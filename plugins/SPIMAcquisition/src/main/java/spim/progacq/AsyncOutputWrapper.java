package spim.progacq;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.awt.Color;
import java.io.File;
import java.lang.InterruptedException;
import java.lang.Thread.UncaughtExceptionHandler;
import java.nio.channels.ClosedByInterruptException;

import org.micromanager.utils.ReportingUtils;

import ij.IJ;
import ij.ImagePlus;
import ij.process.ImageProcessor;
import mmcorej.TaggedImage;

public class AsyncOutputWrapper implements AcqOutputHandler, UncaughtExceptionHandler {
	private static double memPercent() {
		return (double)ij.IJ.currentMemory() / (double)ij.IJ.maxMemory();
	}

	private static class IPC {
		public static enum Type {
			NONE,
			START_STACK,
			MEMORY,
			DISK,
			END_STACK,
		}

		public IPC(Type kind, int tp, int view) {
			if(kind != Type.START_STACK && kind != Type.END_STACK)
				throw new IllegalArgumentException("Slice type specified but no slice information given.");

			this.tp = tp;
			this.view = view;
			this.type = kind;
			x = y = z = t = dt = 0;
			ip = null;
			path = null;
		}

		public IPC(ImageProcessor ip, int tp, int view, double x, double y, double z, double t, double dt) {
			this.tp = tp;
			this.view = view;
			this.type = Type.MEMORY;
			this.ip = ip;
			this.path = null;
			this.x = x;
			this.y = y;
			this.z = z;
			this.t = t;
			this.dt = dt;
		}

		public IPC(File path, int tp, int view, double x, double y, double z, double t, double dt) {
			this.tp = tp;
			this.view = view;
			this.type = Type.DISK;
			this.ip = null;
			this.path = path;
			this.x = x;
			this.y = y;
			this.z = z;
			this.t = t;
			this.dt = dt;
		}

		public final int tp, view;
		public final ImageProcessor ip;
		public final File path;
		public final double x, y, z, t, dt;
		public final Type type;
	}

	private final AcqOutputHandler handler;
	private final Thread writerThread, monitorThread;
	private Exception rethrow;

	private final double memQuota, diskQuota, freedGCRatio;
	private final BlockingQueue<IPC> queue;
	private volatile int onDisk, inMem, freed;
	private final File tempDir, tempRoot;
	private volatile long usedBytes;

	private volatile boolean slowed, finishing;
	private volatile IPC.Type writing = IPC.Type.NONE;

	private Runnable monitorOp = new Runnable() {
		@Override
		public void run() {
			ImageProcessor statusImg = new ij.process.ColorProcessor(400, 200);
			statusImg.setColor(Color.WHITE);
			statusImg.fill();
			statusImg.setColor(Color.BLACK);
			ImagePlus imp = new ImagePlus("Async Status", statusImg);
			imp.show();

			while(!Thread.interrupted() && imp.isVisible()) {
				int n = AsyncOutputWrapper.this.queue.size();
				String statStr = String.format("%d / ram: %d (%.0f%%) / hdd: %d (%.1f%%) / free: %d (%.1f%%) / %s", n, inMem, memPercent()*100, onDisk, diskPercent()*100, freed, 100.0 * (double)freed / (double)(inMem + freed + 1), AsyncOutputWrapper.this.writing.toString().toLowerCase());

				statusImg.copyBits(statusImg, -1, 0, ij.process.Blitter.COPY);

				statusImg.setColor(Color.WHITE);
				statusImg.drawLine(statusImg.getWidth() - 1, 0, statusImg.getWidth() - 1, statusImg.getHeight());
				statusImg.fill(new ij.gui.Roi(0, 0, statusImg.getWidth(), 16));

				statusImg.setColor(Color.BLUE);
				statusImg.drawLine(statusImg.getWidth() - 1, statusImg.getHeight() - 1, statusImg.getWidth() - 1, 16 + (int) ((statusImg.getHeight() - 16) * (1 - memPercent())));

				statusImg.setColor(Color.RED);
				statusImg.drawLine(statusImg.getWidth() - 1, statusImg.getHeight() - 1, statusImg.getWidth() - 1, 16 + (int) ((statusImg.getHeight() - 16) * (1 - diskPercent())));

				statusImg.setColor(AsyncOutputWrapper.this.writing == IPC.Type.MEMORY ? Color.BLUE : (AsyncOutputWrapper.this.writing == IPC.Type.DISK ? Color.RED : Color.BLACK));
				statusImg.drawString(statStr, 4, 16, Color.WHITE);

				imp.updateAndDraw();

				try {
					Thread.sleep(50);
				} catch (InterruptedException e) {
					break;
				}
			}

			imp.close();
		}
	};

	private Runnable writerOp = new Runnable() {
		@Override
		public void run() {
			try {
				while(!Thread.interrupted() && !AsyncOutputWrapper.this.finishing)
				{
					if(AsyncOutputWrapper.this.slowed)
						Thread.sleep(1000);

					handleNext(false);
				}

				handleAll();
			} catch (InterruptedException ie) {
				// Something under writeNext noticed the thread was being interrupted and whined.
				// Log a message/stack trace, but we're too mellow to actually throw a fit.
				ReportingUtils.logError(ie);
			} catch (ClosedByInterruptException cbie) {
				// The writing to disk was interrupted. This can be a serious problem; the last slice in queue probably wasn't
				// written correctly... May need to catch this earlier on; see writeNext?
				ij.IJ.log("Warning: asynchronous writer may have been cancelled before completing. (" + queue.size() + ")");
				ReportingUtils.logError(cbie);
			} catch (Exception e) {
				ij.IJ.log("Async writer failed!");
				throw new RuntimeException(e);
			}
		}
	};

	public AsyncOutputWrapper(File outputDir, AcqOutputHandler handlerRef, double memquota, double diskquota, double freedMemGCRatio, boolean monitor) throws Exception {
		tempDir = new File(outputDir, "async-temp");
		if(!tempDir.exists() && !tempDir.mkdirs())
			throw new Exception("Unable to create temporary directory for async output.");
		else if(tempDir.exists())
			for(File f : tempDir.listFiles())
				if(!f.delete())
					throw new Exception("Unable to clean file " + f.getAbsolutePath());

		tempDir.deleteOnExit();

		File tmp = null;
		for(File root : File.listRoots())
		{
			if(tempDir.getAbsolutePath().startsWith(root.getAbsolutePath()))
			{
				tmp = root;
				break;
			}
		}

		if(tmp == null)
			throw new Exception("Unable to determine output directory filesystem root.");

		tempRoot = tmp;
		usedBytes = 0;

		finishing = false;
		slowed = true;
		handler = handlerRef;
		queue = new LinkedBlockingQueue<IPC>();

		diskQuota = diskquota;
		memQuota = memquota;
		freedGCRatio = freedMemGCRatio;

		onDisk = 0;
		inMem = 0;
		freed = 0;
		writerThread = new Thread(writerOp, "Async Output Handler Thread");
		writerThread.setPriority(Thread.MIN_PRIORITY);
		writerThread.setUncaughtExceptionHandler(this);

		rethrow = null;
		writerThread.start();

		if(monitor) {
			monitorThread = new Thread(monitorOp, "Async Output Monitor Daemon");
			monitorThread.setPriority(Thread.MIN_PRIORITY);
			monitorThread.setDaemon(true);
			monitorThread.start();
		} else {
			monitorThread = null;
		}
	}

	@Override
	public ImagePlus getImagePlus() throws Exception {
		if(rethrow != null)
			throw rethrow;

		handleAll();

		synchronized(handler) {
			return handler.getImagePlus();
		}
	}

	@Override
	public void beginStack(int timepoint, int view) throws Exception {
		if(rethrow != null)
			throw rethrow;

		slowed = true;

		IPC store = new IPC(IPC.Type.START_STACK, timepoint, view);
		if(!queue.offer(store)) {
			handleNext(true);
			queue.put(store);
		}
	}

	@Override
	public void processSlice(int tp, int view, ImageProcessor ip, double X, double Y, double Z,
			double theta, double deltaT) throws Exception {
		if(rethrow != null)
			throw rethrow;

		final IPC store;

		while(memPercent() > memQuota && diskPercent() > diskQuota)
			handleNext(true);

		if(memPercent() < memQuota) {
			store = new IPC(ip, tp, view, X, Y, Z, theta, deltaT);
			++inMem;
		} else {
			File path = File.createTempFile("async_", ".tif", tempDir);
			ImagePlus imp = new ImagePlus("", ip);
			ij.IJ.saveAsTiff(imp, path.getAbsolutePath());
			imp.close();
			path.deleteOnExit();
			store = new IPC(path, tp, view, X, Y, Z, theta, deltaT);
			++onDisk;
			usedBytes += path.length();
		}

		if(!queue.offer(store)) {
			handleNext(true);
			queue.put(store);
		}
	}

	@Override
	public void finalizeStack(int timepoint, int view) throws Exception {
		if(rethrow != null)
			throw rethrow;

		IPC store = new IPC(IPC.Type.END_STACK, timepoint, view);
		if(!queue.offer(store)) {
			handleNext(true);
			queue.put(store);
		}

		slowed = false;
	}

	@Override
	public void finalizeAcquisition() throws Exception {
		if(rethrow != null)
			throw rethrow;

		slowed = false;
		finishing = true; // Tell the writer thread to finish up...
		writerThread.setPriority(Thread.MAX_PRIORITY); // ...and give it more CPU time.

		try {
			// Wait an hour before cancelling the output. Don't force an interrupt unless it's taking forever;
			// interrupts can mess up the output.
			writerThread.join(60 * 60 * 1000);
		} catch(InterruptedException ie) {
			ReportingUtils.logError(ie, "Couldn't keep waiting...");
		} finally {
			if(writerThread.isAlive()) {
				writerThread.interrupt();
				writerThread.join();
				handleAll();
			}

			if(monitorThread != null && monitorThread.isAlive()) {
				monitorThread.interrupt();
				monitorThread.join();
			}
		}

		synchronized(handler) {
			handler.finalizeAcquisition();
		}

		if(usedBytes > 0)
			IJ.log("Warning: Async exited with " + usedBytes + " bytes of HDD still used. Check the output directory temporary async files.");

		if(!tempDir.delete() && usedBytes == 0)
			IJ.log("Notice: Couldn't delete temporary async directory, though it seems to be empty.");
	}

	private synchronized void handleNext(boolean triage) throws Exception {
		if(rethrow != null)
			throw rethrow;

		IPC write = queue.peek();
		if(write != null) {
			queue.take();
			writing = write.type;

			synchronized(handler) {
				switch(write.type){
					case START_STACK: {
						handler.beginStack(write.tp, write.view);
						break;
					}
					case MEMORY: {
						handler.processSlice(write.tp, write.view, write.ip, write.x, write.y, write.z, write.t, write.dt);
						--inMem;
						++freed;

						break;
					}
					case DISK: {
						ImagePlus imp = ij.IJ.openImage(write.path.getAbsolutePath());
						handler.processSlice(write.tp, write.view, imp.getProcessor(), write.x, write.y, write.z, write.t, write.dt);
						imp.close();
						usedBytes -= write.path.length();
						if(!write.path.delete())
							IJ.log("Warning: Couldn't delete temporary async image " + write.path.getAbsolutePath());
						--onDisk;

						break;
					}
					case END_STACK: {
						handler.finalizeStack(write.tp, write.view);
						break;
					}
					case NONE: {
						break;
					}
				}
			}

			double freeratio = (double) freed / (double) (inMem + freed + 1);
			if((triage && freeratio > freedGCRatio/4) || (!slowed && freeratio > freedGCRatio))
			{
				System.gc();
				System.gc();
				freed = 0;
			}

			writing = IPC.Type.NONE;
		}
	}

	private void handleAll() throws Exception {
		if(rethrow != null) {
			if(Thread.currentThread() != writerThread)
				throw rethrow;
			else
				return;
		};

		while(!queue.isEmpty())
			handleNext(false);
	}

	@Override
	public void uncaughtException(Thread thread, Throwable exc) {
		if(thread != writerThread)
			throw new Error("Unexpected exception mis-caught.", exc);

		if(!(exc instanceof Exception))
		{
			ReportingUtils.logError(exc, "Non-exception throwable " + exc.toString() + " caught from writer thread. Wrapping.");
			exc = new Exception("Wrapped throwable; see core log for details: " + exc.getMessage(), exc);
		};

		rethrow = (Exception)exc;
	}

	private double diskPercent() {
		return (double) usedBytes / (double) (usedBytes + tempRoot.getFreeSpace() + 1);
	}
}

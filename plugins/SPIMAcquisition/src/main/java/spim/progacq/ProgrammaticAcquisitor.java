package spim.progacq;

import ij.IJ;
import ij.ImagePlus;

import java.awt.Color;
import java.awt.Rectangle;
import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.EnumMap;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.Vector;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;

import javax.swing.SwingUtilities;

import mmcorej.CMMCore;
import mmcorej.DeviceType;
import mmcorej.TaggedImage;

import org.apache.commons.math3.geometry.euclidean.threed.Vector3D;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.micromanager.MMStudioMainFrame;
import org.micromanager.api.Autofocus;
import org.micromanager.api.IAcquisitionEngine2010;
import org.micromanager.api.MultiStagePosition;
import org.micromanager.api.PositionList;
import org.micromanager.api.ScriptInterface;
import org.micromanager.api.SequenceSettings;
import org.micromanager.api.StagePosition;
import org.micromanager.utils.ChannelSpec;
import org.micromanager.utils.ImageUtils;
import org.micromanager.utils.MDUtils;
import org.micromanager.utils.MMScriptException;
import org.micromanager.utils.ReportingUtils;

import spim.setup.SPIMSetup;
import spim.setup.SPIMSetup.SPIMDevice;
import spim.setup.Stage;
import spim.progacq.AcqRow.ValueSet;

public class ProgrammaticAcquisitor implements IAcquisitionEngine2010 {
	public static final String THETA_POSITION_TAG = "ThetaPositionDeg";

	public static class Profiler {
		private Map<String, Profiler> children;
		private String name;
		private double timer;

		public Profiler(String name) {
			this.name = name;
			children = new java.util.Hashtable<String, Profiler>();
			timer = 0;
		}

		public void start() {
			timer -= System.nanoTime() / 1e9;
		}

		public void stop() {
			timer += System.nanoTime() / 1e9;
		}

		public double getTimer() {
			return timer;
		}

		@Override
		public String toString() {
			return "Profiler(\"" + name + "\")[" + children.size() + " children]";
		}

		public Profiler get(String child) {
			return children.get(child);
		}

		public void create(String child) {
			children.put(child, new Profiler(child));
		}

		public String getLogText() {
			return getLogText(0);
		}

		private static void tabs(StringBuilder sb, int c) {
			for(int i=0; i < c; ++i)
				sb.append("    ");
		}

		protected String getLogText(int indent) {
			StringBuilder sb = new StringBuilder(256);
			tabs(sb, indent);
			sb.append(toString());
			sb.append(": ");
			sb.append(String.format("%.4f", getTimer()));
			sb.append("s\n");

			double childrenTime = 0;
			for(Profiler child : children.values()) {
				tabs(sb, indent);
				childrenTime += child.getTimer();
				sb.append(String.format("%.4f%%:", child.getTimer() / getTimer() * 100));
				sb.append(child.getLogText(indent + 1));
			};

			if(childrenTime > 0.0 && childrenTime < getTimer() && childrenTime/getTimer() < 0.99) {
				tabs(sb, indent);
				sb.append(String.format("%.4f%%: (unaccounted) %.4fs\n", (getTimer() - childrenTime) / getTimer() * 100, getTimer() - childrenTime));
			}

			return sb.toString();
		}
	}

	public static class AcqStatus {
		public static enum Status {
			PRE_INIT,
			SETUP,
			MOVING,
			ACQUIRING,
			WAITING,
			PAUSED,
			TEARDOWN,
			DONE
		}

		// These should only be changed/called from OUTSIDE the acquisition code.
		private volatile boolean pause; // If true, pause the acquisition process at the next available opportunity. Resume once false.

		public void pause(boolean should) {
			pause = should;
		}

		public boolean isPaused() {
			return pause && status == Status.PAUSED;
		}

		public long wakesAt() {
			return status == Status.WAITING ? sequenceResumesAt : -1;
		}

		// These should only be changed/called from INSIDE the acquisition code.
		private volatile Status status; // Current state of the acquisition engine.
		private volatile long sequenceResumesAt; // Milliseconds at which the next sequence starts. -1 if indeterminate/not sleeping.

		public void setup() {
			status = Status.SETUP;
		}

		public void moving() {
			status = Status.MOVING;
		}

		public void snapping() {
			status = Status.ACQUIRING;
		}

		public void sleeping(long until) {
			sequenceResumesAt = until;
			status = Status.WAITING;
		}

		public void paused() {
			status = Status.PAUSED;
		}

		public void teardown() {
			status = Status.TEARDOWN;
		}

		public void done() {
			status = Status.DONE;
		}

		public AcqStatus() {
			pause = false;
			status = Status.PRE_INIT;
			sequenceResumesAt = -1;
		}
	}

	/**
	 * Takes a list of steps and concatenates them together recursively. This is
	 * what builds out rows from a list of lists of positions.
	 * 
	 * @param steps
	 *            A list of lists of discrete values used to make up rows.
	 * @return A list of every possible combination of the input.
	 */
	private static Vector<Vector<Double>> getRows(List<double[]> steps) {
		double[] first = (double[]) steps.get(0);
		Vector<Vector<Double>> rows = new Vector<Vector<Double>>();

		if (steps.size() == 1) {
			for (double val : first) {
				Vector<Double> row = new Vector<Double>();
				row.add(val);
				rows.add(row);
			}
		} else {
			for (double val : first) {
				Vector<Vector<Double>> subrows = getRows(steps.subList(1,
						steps.size()));

				for (Vector<Double> row : subrows) {
					Vector<Double> newRow = new Vector<Double>(row);
					newRow.add(0, val);
					rows.add(newRow);
				}
			}
		}

		return rows;
	}

	/**
	 * Takes a list of ranges (min/step/max triplets), splits them into discrete
	 * values, permutes them, then condenses X/Y into ordered pairs.
	 * 
	 * @param ranges
	 *            List of triplets corresponding to the devices.
	 * @param devs
	 *            List of devices being used (to determine X/Y stages)
	 * @return A list of string arrays, each element being a column for that
	 *         'row'. Can be passed directly into the 'rows' parameter of the
	 *         performAcquisition method.
	 */
	public static List<String[]> generateRowsFromRanges(CMMCore corei,
			List<double[]> ranges, String[] devs) {
		// Each element of range is a triplet of min/step/max.
		// This function determines the discrete values of each range, then
		// works out all possible values and adds them as rows to the table.
		Vector<double[]> values = new Vector<double[]>(ranges.size());

		for (double[] triplet : ranges) {
			double[] discretes = new double[(int) ((triplet[2] - triplet[0]) / triplet[1]) + 1];

			for (int i = 0; i < discretes.length; ++i)
				discretes[i] = triplet[0] + triplet[1] * i;

			values.add(discretes);
		}

		// Build a quick list of indices of X/Y stage devices.
		// Below, we condense the X and Y coordinates into an ordered pair so
		// they can be inserted into the table. This list is used to determine
		// which sets of indices need to be squished into a single value.
		Vector<Integer> xyStages = new Vector<Integer>(devs.length);
		for (int i = 0; i < devs.length; ++i) {
			try {
				if (corei.getDeviceType(devs[i]).equals(
						DeviceType.XYStageDevice))
					xyStages.add(i);
			} catch (Exception e) {
				// I can't think of a more graceless way to resolve this issue.
				// But then, nor can I think of a more graceful one.
				throw new Error("Couldn't resolve type of device \"" + devs[i]
						+ "\"", e);
			}
		}

		Vector<String[]> finalRows = new Vector<String[]>();

		for (List<Double> row : getRows(values)) {
			Vector<String> finalRow = new Vector<String>();

			for (int i = 0; i < row.size(); ++i)
				if (xyStages.contains(i))
					finalRow.add(row.get(i) + ", " + row.get(++i));
				else
					finalRow.add("" + row.get(i));

			finalRows.add(finalRow.toArray(new String[finalRow.size()]));
		}

		return finalRows;
	}

	private static void runDevicesAtRow(SPIMSetup setup, AcqRow row) throws Exception {
		for (SPIMDevice devType : row.getDevices()) {
			spim.setup.Device dev = setup.getDevice(devType);
			ValueSet values = row.getValueSet(devType);

			if (dev instanceof Stage) // TODO: should this be different?
				((Stage)dev).setPosition(values.getStartPosition());
			else
				throw new Exception("Unknown device type for \"" + dev
						+ "\"");
		}

		for (SPIMDevice devType : row.getDevices())
			setup.getDevice(devType).waitFor();
	}

	private static void updateLiveImage(MMStudioMainFrame f, TaggedImage ti)
	{
		try {
			MDUtils.setChannelIndex(ti.tags, 0);
			MDUtils.setFrameIndex(ti.tags, 0);
			MDUtils.setPositionIndex(ti.tags, 0);
			MDUtils.setSliceIndex(ti.tags, 0);
			f.displayImage(ti);
		} catch (Throwable t) {
			ReportingUtils.logError(t, "Attemped to update live window.");
		}
	}

	private static ImagePlus cleanAbort(AcqParams p, boolean live, boolean as) {
		p.getCore().setAutoShutter(as);
		p.getProgressListener().reportProgress(p.getTimeSeqCount() - 1, p.getRows().length - 1, 100.0D);

		try {
			// TEMPORARY: Don't re-enable live mode. This keeps our laser off.
//			MMStudioMainFrame.getInstance().enableLiveMode(live);

			p.getOutputHandler().finalizeAcquisition();
			return p.getOutputHandler().getImagePlus();
		} catch(Exception e) {
			return null;
		}
	}

	private static TaggedImage snapImage(SPIMSetup setup, boolean manualLaser) throws Exception {
		if(manualLaser && setup.getLaser() != null)
			setup.getLaser().setPoweredOn(true);

		TaggedImage ti = setup.getCamera().snapImage();

		if(manualLaser && setup.getLaser() != null)
			setup.getLaser().setPoweredOn(false);

		return ti;
	}

	public interface AcqProgressCallback {
		public abstract void reportProgress(int tp, int row, double overall);
	}

	private final static String SETUP = "Setup";

	private final static String ACQUISITION = "Acquisition";
	private final static String SNAP_IMAGE = "SnapImage";
	private final static String MOVEMENT = "Movement";
	private final static String OUTPUT = "Output";

	private final static String FINALIZATION = "Finalization";

	/**
	 * Performs an acquisition sequence according to the parameters passed.
	 * 
	 *
	 * @param params
	 * @return
	 * @throws Exception
	 */
	public static ImagePlus performAcquisition(final AcqParams params) throws Exception {
		if(params.isContinuous() && params.isAntiDriftOn())
			throw new IllegalArgumentException("No continuous acquisition w/ anti-drift!");

		params.status.setup();

		final Profiler prof = (params.doProfiling() ? new Profiler("performAcquisition") : null);

		if(params.doProfiling())
		{
			prof.create(SETUP);
			prof.create(ACQUISITION);
				prof.get(ACQUISITION).create(SNAP_IMAGE);
				prof.get(ACQUISITION).create(MOVEMENT);
				prof.get(ACQUISITION).create(OUTPUT);
			prof.create(FINALIZATION);
				prof.get(FINALIZATION).create(OUTPUT);

			prof.start();
		}

		if(params.doProfiling())
			prof.get(SETUP).start();

		final SPIMSetup setup = params.getSetup();
		final CMMCore core = setup.getCore();

		final MMStudioMainFrame frame = MMStudioMainFrame.getInstance();
		boolean liveOn = frame.isLiveModeOn();
		if(liveOn)
			frame.enableLiveMode(false);

		boolean autoShutter = core.getAutoShutter();
		core.setAutoShutter(false);

		final SPIMDevice[] metaDevs = params.getMetaDevices();

		final AcqOutputHandler handler = params.getOutputHandler();

		final double acqBegan = System.nanoTime() / 1e9;

		final Map<AcqRow, AntiDrift> driftCompMap;
		if(params.isAntiDriftOn())
			driftCompMap = new HashMap<AcqRow, AntiDrift>(params.getRows().length);
		else
			driftCompMap = null;

		if(params.doProfiling()) {
			prof.get(SETUP).stop();
			prof.get(ACQUISITION).start();
		}

		for(int timeSeq = 0; timeSeq < params.getTimeSeqCount(); ++timeSeq) {
			int step = 0;

			for(final AcqRow row : params.getRows()) {
				while(params.status.isPaused()) {
					params.status.paused();
					Thread.sleep(10);
				}

				final int tp = timeSeq;
				final int rown = step;

				AntiDrift ad = null;
				if(row.getZContinuous() != true && params.isAntiDriftOn()) {
					if((ad = driftCompMap.get(row)) == null) {
						ad = params.getAntiDrift(row);
						ad.setCallback(new AntiDrift.Callback() {
							@Override
							public void applyOffset(Vector3D offs) {
								offs = new Vector3D(offs.getX()*-core.getPixelSizeUm(), offs.getY()*-core.getPixelSizeUm(), -offs.getZ());
								ij.IJ.log(String.format("TP %d view %d: Offset: %s", tp, rown, offs.toString()));
								row.translate(offs);
							}
						});
					}

					ad.startNewStack();
				};

				if(params.doProfiling())
					prof.get(ACQUISITION).get(MOVEMENT).start();

				params.status.moving();
				runDevicesAtRow(setup, row);
				Thread.sleep(params.getSettleDelay());

				if(params.doProfiling())
					prof.get(ACQUISITION).get(MOVEMENT).stop();

				if(params.isIllumFullStack() && setup.getLaser() != null)
					setup.getLaser().setPoweredOn(true);

				if(params.doProfiling())
					prof.get(ACQUISITION).get(OUTPUT).start();

				handler.beginStack(tp, rown);

				if(params.doProfiling())
					prof.get(ACQUISITION).get(OUTPUT).stop();

				if(row.getZStartPosition() == row.getZEndPosition()) {
					if(!params.isContinuous()) {
						params.status.snapping();
						TaggedImage ti = snapImage(setup, !params.isIllumFullStack());

						handleSlice(row, setup, metaDevs, acqBegan, tp, rown, ti, handler, true);
						if(ad != null)
							tallyAntiDriftSlice(core, setup, row, ad, ti);
						if(params.isUpdateLive())
							updateLiveImage(frame, ti);
					};
				} else if (!row.getZContinuous()) {
					double[] positions = row.getValueSet(SPIMDevice.STAGE_Z).values();

					for(int slice = 0; slice < positions.length; ++slice) {
						if(Thread.currentThread().isInterrupted())
							return cleanAbort(params, liveOn, autoShutter);

						while(params.status.isPaused()) {
							params.status.paused();
							Thread.sleep(10);
						}

						if(params.doProfiling())
							prof.get(ACQUISITION).get(MOVEMENT).start();

						params.status.moving();
						setup.getZStage().setPosition(positions[slice]);
						setup.getZStage().waitFor();

						try {
							Thread.sleep(params.getSettleDelay());
						} catch(InterruptedException ie) {
							return cleanAbort(params, liveOn, autoShutter);
						}

						if(params.doProfiling())
							prof.get(ACQUISITION).get(MOVEMENT).stop();

						if(!params.isContinuous()) {
							if(params.doProfiling())
								prof.get(ACQUISITION).get(SNAP_IMAGE).start();

							params.status.snapping();
							TaggedImage ti = snapImage(setup, !params.isIllumFullStack());
							MDUtils.setSliceIndex(ti.tags, slice);

							if(params.doProfiling())
								prof.get(ACQUISITION).get(SNAP_IMAGE).stop();

							if(params.doProfiling())
								prof.get(ACQUISITION).get(OUTPUT).start();

							handleSlice(row, setup, metaDevs, acqBegan, tp, rown, ti, handler, true);

							if(params.doProfiling())
								prof.get(ACQUISITION).get(OUTPUT).stop();

							if(ad != null)
								tallyAntiDriftSlice(core, setup, row, ad, ti);
							if(params.isUpdateLive())
								updateLiveImage(frame, ti);
						}

						double stackProg = Math.max(Math.min((double) slice / (double) positions.length,1),0);
						final Double progress = (double) (params.getRows().length * timeSeq + step + stackProg) / (params.getRows().length * params.getTimeSeqCount());

						SwingUtilities.invokeLater(new Runnable() {
							@Override
							public void run() {
								params.getProgressListener().reportProgress(tp, rown, progress);
							}
						});
					}
				} else {
					setup.getZStage().setPosition(row.getZStartPosition());
					Double oldVel = setup.getZStage().getVelocity();

					setup.getLaser().setPoweredOn(true);

					core.startContinuousSequenceAcquisition(core.getExposure());
					int sequence = 0;

					params.status.moving();
					setup.getZStage().setVelocity(row.getZVelocity());
					setup.getZStage().setPosition(row.getZEndPosition());

					while(!Thread.currentThread().isInterrupted()) {
						if(core.getRemainingImageCount() == 0) {
							if(sequence < 0) {
								break;
							} else {
								continue;
							}
						} else if(sequence >= 0) {
							if(sequence % 5 <= 1 && !setup.getZStage().isBusy()) {
								core.stopSequenceAcquisition();
								sequence = -1;
							} else {
								++sequence;
							}
						}

						if(params.doProfiling())
							prof.get(ACQUISITION).get(OUTPUT).start();

						TaggedImage ti = core.popNextTaggedImage();
						handleSlice(row, setup, metaDevs, acqBegan, tp, rown, ti, handler, false);

						if(params.doProfiling())
							prof.get(ACQUISITION).get(OUTPUT).stop();

						if(params.isUpdateLive())
							updateLiveImage(frame, ti);
					}

					core.stopSequenceAcquisition();

					setup.getZStage().setVelocity(oldVel);
					setup.getLaser().setPoweredOn(false);
				};

				if(params.doProfiling())
					prof.get(ACQUISITION).get(OUTPUT).start();

				handler.finalizeStack(tp, rown);

				if(params.doProfiling())
					prof.get(ACQUISITION).get(OUTPUT).stop();

				if(params.isIllumFullStack() && setup.getLaser() != null)
					setup.getLaser().setPoweredOn(false);

				if(ad != null) {
					ad.finishStack();

					driftCompMap.put(row, ad);
				}

				if(Thread.currentThread().isInterrupted())
					return cleanAbort(params, liveOn, autoShutter);

				final Double progress = (double) (params.getRows().length * timeSeq + step + 1)
						/ (params.getRows().length * params.getTimeSeqCount());

				SwingUtilities.invokeLater(new Runnable() {
					@Override
					public void run() {
						params.getProgressListener().reportProgress(tp, rown, progress);
					}
				});

				++step;
			}

			if(timeSeq + 1 < params.getTimeSeqCount()) {
				double wait = (params.getTimeStepSeconds() * (timeSeq + 1)) -
						(System.nanoTime() / 1e9 - acqBegan);
	
				if(wait > 0D)
					try {
						params.status.sleeping(System.currentTimeMillis() + (long)(wait * 1e3));
						Thread.sleep((long)(wait * 1e3));
					} catch(InterruptedException ie) {
						return cleanAbort(params, liveOn, autoShutter);
					}
				else
					core.logMessage("Behind schedule! (next seq in "
							+ Double.toString(wait) + "s)");
			}
		}

		params.status.teardown();

		if(params.doProfiling()) {
			prof.get(ACQUISITION).stop();
			prof.get(FINALIZATION).start();
			prof.get(FINALIZATION).get(OUTPUT).start();
		}

		handler.finalizeAcquisition();

		if(params.doProfiling())
			prof.get(FINALIZATION).get(OUTPUT).stop();

		if(autoShutter)
			core.setAutoShutter(true);

		// TEMPORARY: Don't re-enable live mode. This keeps our laser off.
//		if(liveOn)
//			frame.enableLiveMode(true);

		if(params.doProfiling())
		{
			prof.get(FINALIZATION).stop();
			prof.stop();
			ij.IJ.log(prof.getLogText());
		}

		params.status.done();
		return handler.getImagePlus();
	}

	private static void tallyAntiDriftSlice(CMMCore core, SPIMSetup setup, AcqRow row, AntiDrift ad, TaggedImage ti) throws Exception {
		ad.tallySlice(new Vector3D(0,0,setup.getZStage().getPosition()-row.getZStartPosition()), ti);
	}

	private static void handleSlice(AcqRow row, SPIMSetup setup,
			SPIMDevice[] metaDevs, double start, int timepoint,
			int rownum, TaggedImage img, AcqOutputHandler handler,
			boolean stageMetadataValid) throws Exception {

		MDUtils.setElapsedTimeMs(img.tags, 1e3 * (System.nanoTime() / 1e9 - start));
		MDUtils.setFrameIndex(img.tags, timepoint);
		MDUtils.setPositionIndex(img.tags, rownum);

		if(stageMetadataValid) {
			Vector3D pos = setup.getPosition();
			MDUtils.setXPositionUm(img.tags, pos.getX());
			MDUtils.setYPositionUm(img.tags, pos.getY());
			MDUtils.setZPositionUm(img.tags, pos.getZ());
			img.tags.put(THETA_POSITION_TAG, setup.getAngle());
		} else {
			MDUtils.setXPositionUm(img.tags, 0);
			MDUtils.setYPositionUm(img.tags, 0);
			MDUtils.setZPositionUm(img.tags, 0);
			img.tags.put(THETA_POSITION_TAG, 0);
		}

		handler.processSlice(img);
	}

	/**
	 * 
	 * Micro-Manager Acquisition Engine Implementation
	 * 
	 */

	private class AcqAdapter implements AcqOutputHandler {
		private final BlockingQueue<TaggedImage> imageQueue;

		public AcqAdapter(BlockingQueue<TaggedImage> queue) {
			this.imageQueue = queue;
		}

		@Override
		public ImagePlus getImagePlus() throws Exception {
			return null;
		}

		@Override
		public void beginStack(int timepoint, int view) throws Exception {
			// no-op
		}

		@Override
		public void processSlice(TaggedImage img) throws Exception {
			ProgrammaticAcquisitor.this.acqStep(
					MDUtils.getFrameIndex(img.tags),
					MDUtils.getPositionIndex(img.tags),
					MDUtils.getChannelIndex(img.tags),
					MDUtils.getSliceIndex(img.tags),
					img.tags
			);

			imageQueue.put(img);
		}

		@Override
		public void finalizeStack(int timepoint, int view) throws Exception {
			// no-op
		}

		@Override
		public void finalizeAcquisition() throws Exception {
			// no-op
		}
	}

	private final ScriptInterface scriptInterface;
	private Thread acquisitionThread;
	private boolean stop;
	private AcqParams params;
	private SPIMSetup setup;
	private JSONObject summary;
	private final Map<int[], Runnable> tasks;

	// This constructor signature matches that of the Clojure implementation of AcquisitionEngine2010.
	public ProgrammaticAcquisitor(ScriptInterface scri)
	{
		scriptInterface = scri;
		stop = false;

		summary = new JSONObject();
		setup = SPIMSetup.createDefaultSetup(scri.getMMCore());
		tasks = new HashMap<int[], Runnable>(); // I'm abusing the 'key' concept, here; arrays hash to their pointer, so every key will be unique.
	}

	@Override
	public void attachRunnable(int timepoint, int row, int channel, int slice, Runnable task) {
		tasks.put(new int[] { timepoint, row, channel, slice }, task);
	}

	private void acqStep(int timepoint, int row, int channel, int slice, JSONObject tags) throws Exception {
		for(Map.Entry<int[], Runnable> task : tasks.entrySet()) {
			int[] key = task.getKey();
			if((key[0] != -1 && key[0] != timepoint) || (key[1] != -1 && key[1] != row) || (key[2] != -1 && key[2] != channel) || (key[3] != -1 && key[3] != slice))
				continue;

			task.getValue().run();
		}

		// These metadata are required by MM, but not added by ProgAcq by default. I may still move these up to handleSlice.
		MDUtils.setChannelName(tags, "Channel " + channel);
		MDUtils.setChannelColor(tags, Color.WHITE.getRGB());
		MDUtils.setChannelIndex(tags, channel);

		// These are the extra metadata MM silently requires. Curiously there are no calls in MDUtils to set them.
		tags.put("Time", new SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z").format(new Date()));
	}

	@Override
	public void clearRunnables() {
		tasks.clear();
	}

	@Override
	public JSONObject getSummaryMetadata() {
		return summary;
	}

	@Override
	public boolean isFinished() {
		return !acquisitionThread.isAlive();
	}

	@Override
	public boolean isPaused() {
		return params.status.isPaused();
	}

	@Override
	public boolean isRunning() {
		return acquisitionThread.isAlive();
	}

	@Override
	public long nextWakeTime() {
		return params.status.wakesAt();
	}

	@Override
	public void pause() {
		params.status.pause(true);
	}

	@Override
	public void resume() {
		params.status.pause(false);
	}

	@Override
	public BlockingQueue<TaggedImage> run(SequenceSettings ss) {
		return run(ss, true);
	}

	@Override
	public BlockingQueue<TaggedImage> run(SequenceSettings ss, boolean cleanup) {
		try {
			return run(ss, cleanup, scriptInterface.getPositionList(), scriptInterface.getAutofocusManager().getDevice());
		} catch (MMScriptException e) {
			ReportingUtils.logError(e);
			return null;
		}
	}

	private static interface ObjectMap<K, V> {
		public V map(K on);
	}

	private static <K, V> List<V> map(Iterable<K> from, ObjectMap<K, V> func) {
		List<V> out = new LinkedList<V>();

		for(K k : from)
			out.add(func.map(k));

		return out;
	}

	private static JSONArray summarizePositionList(PositionList poslist) {
		return new JSONArray(
			map(Arrays.asList(poslist.getPositions()),
				new ObjectMap<MultiStagePosition, JSONObject>() {
					@Override
					public JSONObject map(MultiStagePosition msp) {
						try {
							JSONObject obj = new JSONObject();
							obj.put("Label", msp.getLabel());
							obj.put("GridRowIndex", msp.getGridRow());
							obj.put("GridColumnIndex", msp.getGridColumn());

							Map<String, JSONArray> mspmap = new HashMap<String, JSONArray>();
							for(int i = 0; i < msp.size(); ++i) {
								switch(msp.get(i).numAxes) {
								case 1:
									mspmap.put(msp.get(i).stageName, new JSONArray(Arrays.asList(msp.get(i).x)));
									break;
								case 2:
									mspmap.put(msp.get(i).stageName, new JSONArray(Arrays.asList(msp.get(i).x, msp.get(i).y)));
									break;
								case 3:
								default:
									mspmap.put(msp.get(i).stageName, new JSONArray(Arrays.asList(msp.get(i).x, msp.get(i).y, msp.get(i).z)));
									break;
								}
							}
							obj.put("DeviceCoordinatesUm", new JSONObject(mspmap));

							return obj;
						} catch(JSONException jse) {
							throw new Error(jse);
						}
					}
				}
			)
		);
	}

	@Override
	public BlockingQueue<TaggedImage> run(SequenceSettings ss, boolean cleanup, PositionList poslist, Autofocus afdev) {
		try {
			summary = new JSONObject();

			summary.put("BitDepth", setup.getCore().getImageBitDepth());
			summary.put("Channels", Math.max(1, ss.channels.size()));
			if(ss.channels.size() > 0) {
				summary.put("ChNames", new JSONArray(map(ss.channels, new ObjectMap<ChannelSpec, String>() { public String map(ChannelSpec on) { return on.toString(); } })));
				summary.put("ChColors", new JSONArray(map(ss.channels, new ObjectMap<ChannelSpec, Integer>() { public Integer map(ChannelSpec on) { return on.color.getRGB(); } })));
				summary.put("ChContrastMax", new JSONArray(map(ss.channels, new ObjectMap<ChannelSpec, Integer>() { public Integer map(ChannelSpec on) { return on.contrast.max; } })));
				summary.put("ChContrastMin", new JSONArray(map(ss.channels, new ObjectMap<ChannelSpec, Integer>() { public Integer map(ChannelSpec on) { return on.contrast.min; } })));
			} else {
				summary.put("ChNames", new JSONArray(Arrays.asList("Default")));
				summary.put("ChColors", new JSONArray(Arrays.asList(Color.WHITE.getRGB())));
				summary.put("ChContrastMax", new JSONArray(Arrays.asList(65535)));
				summary.put("ChContrastMin", new JSONArray(Arrays.asList(0)));
			}
			summary.put("Comment", ss.comment);
			summary.put("ComputerName", java.net.InetAddress.getLocalHost().getHostName());
			summary.put("Depth", setup.getCore().getBytesPerPixel());
			summary.put("Directory", ss.save ? ss.root : "");
			summary.put("Frames", Math.max(1, ss.numFrames));
			summary.put("GridColumn", 0);
			summary.put("GridRow", 0);
			summary.put("Height", setup.getCore().getImageHeight());
			summary.put("InitialPositionList", ss.usePositionList ? summarizePositionList(poslist) : null);
			summary.put("Interval_ms", ss.intervalMs);
			summary.put("CustomIntervals_ms", ss.customIntervalsMs == null ? new JSONArray() : new JSONArray(ss.customIntervalsMs));
			summary.put("IJType", ImageUtils.BppToImageType(setup.getCore().getBytesPerPixel()));
			summary.put("KeepShutterOpenChannels", ss.keepShutterOpenChannels);
			summary.put("KeepShutterOpenSlices", ss.keepShutterOpenSlices);
			summary.put("MicroManagerVersion", scriptInterface != null ? scriptInterface.getVersion() : setup.getCore().getVersionInfo());
			summary.put("MetadataVersion", 10);
			summary.put("PixelAspect", 1.0);
			summary.put("PixelSize_um", setup.getCore().getPixelSizeUm());
			summary.put("PixelType", (setup.getCore().getNumberOfComponents() > 1 ? "RGB" : "GRAY") + (setup.getCore().getNumberOfComponents() * setup.getCore().getBytesPerPixel() * 8));
			summary.put("Positions", Math.min(1, poslist.getNumberOfPositions()));
			summary.put("Prefix", ss.save ? ss.prefix : "");
			Rectangle roi = setup.getCore().getROI();
			summary.put("ROI", roi == null ? new JSONArray() : new JSONArray(Arrays.asList(roi.x, roi.y, roi.width, roi.height)));
			summary.put("Slices", Math.max(1, ss.slices.size()));
			summary.put("SlicesFirst", ss.slicesFirst);
			summary.put("Source", "Micro-Manager");
			summary.put("TimeFirst", ss.timeFirst);
			summary.put("UserName", System.getProperty("user.name"));
			summary.put("UUID", UUID.randomUUID());
			summary.put("Width", setup.getCore().getImageWidth());
			summary.put("z-step_um", ss.slices.get(1) - ss.slices.get(0));

			summary.put("Prefix", ss.prefix);
		} catch (Exception e) {
			ReportingUtils.showError(e);
		}

		File outputDir = new File(ss.root);
		if(!outputDir.exists())
			if(!outputDir.mkdirs())
				return null;

		List<AcqRow> rows = new LinkedList<AcqRow>();
		for(MultiStagePosition msp : poslist.getPositions()) {
			Map<SPIMDevice, ValueSet> devs = new EnumMap<SPIMDevice, ValueSet>(SPIMDevice.class);

			for(SPIMDevice devkey : SPIMDevice.values()) {
				spim.setup.Device dev = setup.getDevice(devkey);

				if(dev == null)
					continue;

				StagePosition sp = msp.get(dev.getLabel());

				if(sp == null)
					continue;

				if(devkey.equals(SPIMDevice.STAGE_Z))
				{
					double[] points = new double[ss.slices.size()];
					for(int i=0; i < points.length; ++i)
						points[i] = ss.slices.get(i);

					devs.put(devkey, new ValueSet(points));
				}
				else
				{
					switch(sp.numAxes) {
					case 1:
						devs.put(devkey, new ValueSet(sp.x));
						break;
					case 2:
						devs.put(devkey, new ValueSet(devkey == SPIMDevice.STAGE_X ? sp.x : sp.y));
						break;
					case 3:
					default:
						throw new Error("Unusual axis count " + sp.numAxes);
					}
				}
			}

			rows.add(new AcqRow(devs));
		}

		BlockingQueue<TaggedImage> queue = new LinkedBlockingQueue<TaggedImage>();

		params = new AcqParams(
			setup.getCore(),
			setup,
			rows.toArray(new AcqRow[rows.size()]),
			ss.intervalMs / 1e3,
			ss.numFrames,
			false,
			new AcqProgressCallback() {
				 @Override
				public void reportProgress(int tp, int row, double overall) {
					 ReportingUtils.logMessage(String.format("Acquisition progress at %.2f percent (T %d R %d)...", overall*100, tp, row));
				}
			},
			null,
			new AcqAdapter(queue)
		);

		new Thread("ProgrammaticAcquisitor MM Acquisition Thread") {
			@Override
			public void run() {
				try {
					ProgrammaticAcquisitor.performAcquisition(params);
				} catch (Exception e) {
					ReportingUtils.logError(e, "Error while acquiring.");
				}
			}
		}.start();

		return queue;
	}

	@Override
	public void stop() {
		stop = true;
		acquisitionThread.interrupt();
	}

	@Override
	public boolean stopHasBeenRequested() {
		return stop;
	}
};

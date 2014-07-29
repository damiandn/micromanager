package spim.setup;

import java.util.Arrays;
import java.util.List;
import java.util.Random;

import mmcorej.CMMCore;
import mmcorej.DeviceType;
import mmcorej.TaggedImage;
import net.imagej.ops.Op;
import net.imglib2.Cursor;
import net.imglib2.img.Img;
import net.imglib2.img.array.ArrayImgs;
import net.imglib2.type.numeric.RealType;
import net.imglib2.type.numeric.integer.UnsignedByteType;

import org.apache.commons.math3.geometry.euclidean.threed.Rotation;
import org.apache.commons.math3.geometry.euclidean.threed.Vector3D;
import org.json.JSONException;
import org.micromanager.utils.ReportingUtils;
import org.scijava.Context;
import org.scijava.ItemIO;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.plugin.PluginInfo;

import spim.setup.SPIMSetup.SPIMDevice;

public class DemoCamera extends Camera {
	@Plugin(type = Op.class, name = "gaussiandot")
	public static class GaussianDot<T extends RealType<T>> implements Op {
		@Parameter(type = ItemIO.BOTH)
		private Img<T> image;

		@Parameter
		private Double x, y, outerRadius, innerRadius, minIntensity, maxIntensity;

		@Override
		public void run() {
			Cursor<T> iter = image.localizingCursor();
			double[] pos = new double[] { 0, 0 };
			while(iter.hasNext()) {
				T pix = iter.next();
				iter.localize(pos);

				if(pos[0] < x - 2*outerRadius || pos[0] > x + 2*outerRadius || pos[1] < y - 2*outerRadius || pos[1] > y + 2*outerRadius)
					continue;

				final double i = minIntensity + (maxIntensity - minIntensity)*Math.random();
				final double lr = Math.sqrt(Math.pow(pos[0] - x, 2) + Math.pow(pos[1] - y, 2));
				pix.setReal(Math.min(pix.getMaxValue(), pix.getRealDouble() + pix.getMaxValue() * i * Math.exp(-Math.pow(Math.max(0, lr - innerRadius)/outerRadius, 2))));
			}
		}
	}

	public static class Factory implements Device.Factory {
		@Override
		public String deviceName() {
			return "DCam";
		}

		@Override
		public Iterable<SPIMDevice> deviceTypes() {
			return Arrays.asList(SPIMDevice.CAMERA1, SPIMDevice.CAMERA2);
		}

		@Override
		public Device manufacture(CMMCore core, String label) {
			return new DemoCamera(core, label);
		}

		// HACK mmplugins isn't on the IJ2 classpath, so the plugins.json isn't parsed automatically.
		// We register the op here manually; future versions of MM might remove the need for this.
		// (Still need the OpService, though.)
		static {
			Context context = net.imagej.legacy.IJ1Helper.getLegacyContext();
			op = context.service(net.imagej.ops.OpService.class);
			context.service(org.scijava.plugin.PluginService.class).addPlugin(new PluginInfo<Op>(GaussianDot.class, Op.class, GaussianDot.class.getAnnotation(Plugin.class)));
		}
	}

	protected static net.imagej.ops.OpService op;

	public static double zNear = 1.0;
	public static double zFar = 10000.0;
	public static double fov = Math.PI/200;

	public static double focus = 3000.0;
	public static double lsWidth = 15.0;
	public static double beadRadius = 1.5;

	public static long seed = 0xFEEDBEEF;
	public static int count = 4000;
	public static int blobComplex = 5;

	public static double columnRadius = 512;
	public static double columnHeight = 1024;
	public static double driftMaxAmpl = 512;
	public static double blobSize = 200;
	public static double blobbingDistance = 1.45;

	private Vector3D[] beadPoints;
	private Vector3D[] blobVectors;
	private Vector3D axis;
	private Vector3D driftVector;
	private Double tStart;
	private Rotation cachedRotation;

	public DemoCamera(CMMCore core, String label) {
		super(core, label);

		axis = new Vector3D(1500, 1500, focus);
		cachedRotation = null;

		// Need a reliably random generation for any consistent testing.
		Random rng = new Random(seed);

		beadPoints = new Vector3D[count];
		for(int i = 0; i < count; ++i) {
			final double theta = rng.nextDouble() * Math.PI * 2;
			beadPoints[i] = new Vector3D(Math.cos(theta), 0, Math.sin(theta)).scalarMultiply((1 - 0.9*rng.nextDouble())*columnRadius).add(new Vector3D(0, 2*columnHeight*(rng.nextDouble() - 0.5), 0));
		}

		blobVectors = new Vector3D[blobComplex];
		for(int i = 0; i < blobComplex; ++i) {
			blobVectors[i] = new Vector3D(rng.nextDouble() * 2*Math.PI, Math.PI*(rng.nextDouble() - 0.5)).scalarMultiply(blobSize*(rng.nextDouble() + 3)/4);
		}

		driftVector = new Vector3D(rng.nextDouble() * 2*Math.PI, Math.PI*(rng.nextDouble() - 0.5)).scalarMultiply(driftMaxAmpl*(rng.nextDouble() + 3)/4);
		tStart = null;
	}

	// The CAMERA is at 'axis' looking along (0, 0, 1)
	// The SAMPLE CENTER is at 'stage'

	protected Vector3D fullTransform(int w, int h, double umperpix, Vector3D stage, double theta, Vector3D sampleSpace) {
		if(cachedRotation == null || cachedRotation.getAngle() != theta)
			cachedRotation = new Rotation(Vector3D.PLUS_J, -theta * Math.PI/180.0);

		// World:
		sampleSpace = cachedRotation.applyTo(sampleSpace).add(stage);

		// Camera:
		sampleSpace = sampleSpace.subtract(axis);

		// Projection:
		return new Vector3D(sampleSpace.getX()/umperpix + w/2.0, sampleSpace.getY()/umperpix + h/2.0, sampleSpace.getZ());
	}

	protected <T extends RealType<T>> void paintBead(Img<T> img, double umperpix, Vector3D screenspace) {
		final double x = screenspace.getX();
		final double y = screenspace.getY();
		final double z = screenspace.getZ();

		if((int)x < 0 || (int)x >= img.dimension(0) || (int)y < 0 || (int)y >= img.dimension(1) || Math.abs(z) > lsWidth)
			return;

		final double radius = beadRadius * Math.exp(Math.pow(z/(0.45*lsWidth), 2)) / umperpix;
		final double intensity = Math.max(0, 1 - radius/(4*beadRadius));

		op.run(GaussianDot.class, img, x + 2*beadRadius, y + 2*beadRadius, radius, beadRadius / umperpix, intensity*3/4, intensity);
	}

	protected <T extends RealType<T>> void paintBlob(Img<T> img, double umperpix, double angle, Vector3D stage) {
		final Rotation rot = new Rotation(Vector3D.PLUS_J, angle * Math.PI/180.0);

		if(Math.abs(stage.getZ() - axis.getZ()) > 1.5*blobSize)
			return;

		Cursor<T> iter = img.localizingCursor();
		final double[] loc2d = new double[] {0, 0};
		while(iter.hasNext()) {
			T pix = iter.next();
			iter.localize(loc2d);

			final Vector3D loc = axis.add(new Vector3D((loc2d[0] - img.dimension(0)/2)*umperpix, (loc2d[1] - img.dimension(1)/2)*umperpix, 0));

			if(Math.abs(loc.getX() - stage.getX()) > 1.5*blobSize || Math.abs(loc.getY() - stage.getY()) > 1.5*blobSize)
				continue;

			double dist = blobbingDistance;
			for(final Vector3D vec : blobVectors)
				dist *= rot.applyTo(vec).add(stage).distanceSq(loc) / vec.getNormSq();

			pix.setReal(Math.min(pix.getMaxValue(), pix.getRealDouble() + pix.getMaxValue() * (Math.random() + 15)/16 * (1 - Math.abs(Math.min(Math.max(-0.6, (dist - 1)/0.3), 1)))));
		}
	}

	@Override
	public TaggedImage snapImage() {
		final double umperpix = core.getPixelSizeUm();

		if(tStart == null)
			tStart = (double)System.currentTimeMillis();

		Vector3D stage;
		double angle;
		try {
			stage = new Vector3D(core.getXPosition(core.getXYStageDevice()), core.getYPosition(core.getXYStageDevice()), core.getPosition(core.getFocusDevice()) /*- focus*/);
			List<String> twister = Arrays.asList(core.getLoadedDevicesOfType(DeviceType.StageDevice).toArray());
			angle = core.getPosition(twister.get(0).equals(core.getFocusDevice()) ? twister.get(1) : twister.get(0));
		} catch (Exception e1) {
			ReportingUtils.logException("snapping image", e1);
			return null;
		}

		int w = Integer.parseInt(getProperty("OnCameraCCDXSize"));
		int h = Integer.parseInt(getProperty("OnCameraCCDYSize"));

		ij.process.ByteProcessor shp = new ij.process.ByteProcessor(w, h);
		byte[] pix = (byte[]) shp.getPixels();
		Img<UnsignedByteType> img = ArrayImgs.unsignedBytes(pix, w, h);

		stage = stage.add(driftVector.scalarMultiply(drift(((double)System.currentTimeMillis() - tStart) / (30.0*60.0*1000.0))));

		for(Vector3D bead : beadPoints)
			paintBead(img, umperpix, fullTransform(w, h, umperpix, stage, angle, bead));

		paintBlob(img, core.getPixelSizeUm(), angle, stage);

		TaggedImage ti = org.micromanager.utils.ImageUtils.makeTaggedImage(shp);

		try {
			ti.tags.put("BitDepth", Integer.parseInt(getProperty("BitDepth")));
			org.micromanager.utils.MDUtils.setPixelSizeUm(ti.tags, core.getPixelSizeUm());
		} catch (JSONException e) {
			ReportingUtils.logError(e, "while assembling taggedimage");
		}

		return ti;
	}

	protected double drift(double t) {
		// 0.5*(1-Math.cos(t * 2*Math.PI))
		return 1 - 1/(t + 1);
	}
}

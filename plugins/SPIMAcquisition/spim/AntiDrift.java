package spim;

import ij.IJ;
import ij.process.ImageProcessor;

import org.apache.commons.math.geometry.euclidean.threed.Vector3D;
import org.micromanager.MMStudioMainFrame;

import spim.progacq.AcqRow;

public class AntiDrift {
	private Vector3D initialMean, cachedMean, runningMean, counts;
	private double runningIntensity, currentMin, currentMax;
	private long counted;
	private double midz;

	private double lowThresh, highThresh;
	private double oldWeight;
	private boolean autoThreshold;
	private double resetMagnitude;
	private boolean absolute;
	private boolean visualize;

	public AntiDrift() {
		initialMean = Vector3D.ZERO;
		cachedMean = Vector3D.ZERO;
		runningMean = Vector3D.ZERO;
		runningIntensity = 0.0D;
		currentMin = Double.MAX_VALUE;
		currentMax = Double.MIN_VALUE;
		counted = 0;
		lowThresh = -1;
	}

	public AntiDrift(double[] parameters, double dz) {
		this();

		autoThreshold = (parameters[0] > 0);
		lowThresh = parameters[1];
		highThresh = parameters[2];

		oldWeight = parameters[3] / 2;
		resetMagnitude = parameters[4];
		absolute = (parameters[5] > 0);

		visualize = (parameters[6] > 0);

		midz = dz / 2;
	}

	public Vector3D getIntensityCenter() {
		if(counted / (counts.getX()*counts.getY()/counts.getZ()) < 0.0001) {
			ij.IJ.log("(Insufficient data to calculate anti-drift.)");
			return initialMean;
		}

		Vector3D ci = runningMean.scalarMultiply(1/runningIntensity);

		if(resetMagnitude > 0 && initialMean.subtract(ci).getNorm() > resetMagnitude) {
			ij.IJ.log("(Magnitude exceeded reset; halving.)");
			ci = ci.add(initialMean).scalarMultiply(0.5);
		}

		return ci;
	}

	public Vector3D getAntiDriftOffset() {
		if(absolute && !initialMean.equals(cachedMean))
			return new Vector3D(0, 0, 2*midz).subtract(initialMean.add(cachedMean));
		else if(absolute)
			return new Vector3D(0, 0, midz).subtract(initialMean);
		else
			return initialMean.subtract(cachedMean);
	}

	public void startNewStack() {
		runningMean = Vector3D.ZERO;
		counts = Vector3D.ZERO;
		runningIntensity = 0.0D;
		currentMin = Double.MAX_VALUE;
		currentMax = Double.MIN_VALUE;
		counted = 0;
	}

	public void tallySlice(Vector3D center, ImageProcessor ip) {
		counts = counts.add(new Vector3D(ip.getWidth(), ip.getHeight(), 1));

		// The - center.get*() become positive in the subtraction below.
		double cx = ip.getWidth()/2 - center.getX();
		double cy = ip.getHeight()/2 - center.getY();

		double it = 0;

		for(int y=0; y < ip.getHeight(); ++y) {
			for(int x=0; x < ip.getWidth(); ++x) {
				double pv = ip.getPixelValue(x,y);

				if(!autoThreshold)
				{
					if(pv < lowThresh || pv > highThresh)
					{
						pv = 0;
					}
					else
					{
						pv = lowThresh;
						++counted;
					}
				}
				else
				{
					pv -= (ip.getMin() + (ip.getMax() - ip.getMin())*lowThresh);
					if(pv <= 0 || pv > (ip.getMax() - ip.getMin())*(highThresh - lowThresh))
						pv = 0;
					else
						++counted;
				}

				runningMean = runningMean.add(new Vector3D((x - cx)*pv, (y - cy)*pv, 0));

				it += pv;

				if(pv < currentMin)
					currentMin = pv;

				if(pv > currentMax)
					currentMax = pv;
			}
		}

		runningIntensity += it;
		runningMean = runningMean.add(new Vector3D(0,0,center.getZ()*it));

		if(visualize)
			visualizeThreshold(ip);
	}

	public void finishStack() {
		finishStack(initialMean.getNorm() == 0);
	}

	public void finishStack(boolean initial) {
		if(cachedMean == null)
			cachedMean = getIntensityCenter();
		else
			cachedMean = cachedMean.scalarMultiply(oldWeight).add(getIntensityCenter().scalarMultiply(1-oldWeight));

		ij.IJ.log("Counted pixels: " + counted + "/" + (counts.getX()*counts.getY()/counts.getZ()) + " (" + (counted/(counts.getX()*counts.getY()/counts.getZ())) + ")");

		if(initial)
			initialMean = cachedMean;
	}

	private void visualizeThreshold(ImageProcessor ip) {
		for(int y = 0; y < ip.getHeight(); ++y)
			for(int x = 0; x < ip.getWidth(); ++x)
				if((autoThreshold &&
					(ip.getPixelValue(x, y) < (ip.getMin() + (ip.getMax() - ip.getMin())*lowThresh) ||
					ip.getPixelValue(x, y) > (ip.getMin() + (ip.getMax() - ip.getMin())*highThresh))) ||
					(!autoThreshold &&
					(ip.getPixelValue(x, y) < lowThresh || ip.getPixelValue(x, y) > highThresh)))
					ip.set(x, y, (int) ip.getMin());
				else
					ip.set(x, y, (int) ip.getMax());

		MMStudioMainFrame.getInstance().getImageWin().getImagePlus().setProcessor(ip);
	}
}

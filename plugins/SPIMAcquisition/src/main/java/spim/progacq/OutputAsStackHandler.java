package spim.progacq;

import ij.ImagePlus;
import ij.ImageStack;
import ij.process.ImageProcessor;

public class OutputAsStackHandler implements AcqOutputHandler {
	private ImageStack stack;
	private ImagePlus img;

	public OutputAsStackHandler() {
		stack = null;
		img = null;
	}

	@Override
	public void processSlice(int timepoint, int view, ImageProcessor ip, double X, double Y, double Z, double theta, double deltaT)
			throws Exception {
		if(stack == null)
			stack = new ImageStack(ip.getWidth(), ip.getHeight());

		stack.addSlice("t=" + deltaT, ip);
	}

	@Override
	public void finalizeAcquisition() throws Exception {
		img = new ImagePlus("SimpleOutput", stack);
	}

	@Override
	public ImagePlus getImagePlus() throws Exception {
		return (img != null ? img : new ImagePlus("SimpleOutput", stack));
	}

	@Override
	public void finalizeStack(int timepoint, int view) throws Exception {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void beginStack(int timepoint, int view) throws Exception {
		// TODO Auto-generated method stub
		
	}
}

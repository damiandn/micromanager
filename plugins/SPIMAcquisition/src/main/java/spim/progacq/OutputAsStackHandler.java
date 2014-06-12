package spim.progacq;

import java.util.LinkedList;
import java.util.List;

import mmcorej.TaggedImage;

import org.json.JSONObject;
import org.micromanager.utils.ImageUtils;
import org.micromanager.utils.MDUtils;

import ij.ImagePlus;
import ij.ImageStack;

public class OutputAsStackHandler implements AcqOutputHandler {
	private ImageStack stack;
	private ImagePlus img;
	private String accumulatedTags;

	public OutputAsStackHandler() {
		stack = null;
		img = null;
		accumulatedTags = "";
	}

	@Override
	public void processSlice(TaggedImage img)
			throws Exception {
		if(stack == null)
			stack = new ImageStack(MDUtils.getWidth(img.tags), MDUtils.getHeight(img.tags));

		stack.addSlice("t=" + MDUtils.getElapsedTimeMs(img.tags), ImageUtils.makeProcessor(img));
		accumulatedTags += img.tags.toString(1) + ",\n";
	}

	@Override
	public void finalizeAcquisition() throws Exception {
		img = new ImagePlus("SimpleOutput", stack);
		img.setProperty("Info", accumulatedTags);
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

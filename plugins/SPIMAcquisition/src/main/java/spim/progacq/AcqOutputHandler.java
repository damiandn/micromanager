package spim.progacq;

import mmcorej.TaggedImage;
import ij.ImagePlus;

public interface AcqOutputHandler {
	/**
	 * Gets the current state of the acquisition sequence as represented by an
	 * ImagePlus. This can be called in the middle of acquisition, prior to
	 * finalize (specifically if acquisition is interrupted).
	 *
	 * @return an ImagePlus representing the acquisition
	 * 
	 * @throws Exception
	 */
	public abstract ImagePlus getImagePlus() throws Exception;

	/**
	 * Called by the acquisition code when about to begin snapping a new stack.
	 * 
	 * @param timepoint Beginning time index
	 * @param view Beginning view index
	 * 
	 * @throws Exception
	 */
	void beginStack(int timepoint, int view) throws Exception;

	/**
	 * Handle the next slice as output by the acquisition code. What this means
	 * obviously depends largely on implementation.
	 * 
	 * @param img A TaggedImage containing the image data, tagged as by MDUtils with the stage position, timepoint (frame), and view index (position index). See ProgrammaticAcquisitor.handleSlice.
	 * 
	 * @throws Exception
	 */
	public abstract void processSlice(TaggedImage img) throws Exception;

	/**
	 * A stack has finished being acquired; react accordingly.
	 * 
	 * @param timepoint Beginning time index
	 * @param view Beginning view index
	 * 
	 * @throws Exception
	 */
	public abstract void finalizeStack(int timepoint, int view) throws Exception;

	/**
	 * The acquisition has ended; do any clean-up and finishing steps (such as
	 * saving the collected data to a file). IMPORTANT: After a call to finalize
	 * the handler should be in a state where it can accept new slices as an
	 * entirely different acquisition.
	 *
	 * @throws Exception
	 */
	public abstract void finalizeAcquisition() throws Exception;

}

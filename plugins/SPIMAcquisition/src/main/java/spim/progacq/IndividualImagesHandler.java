package spim.progacq;

import java.io.File;
import java.util.Iterator;

import mmcorej.TaggedImage;

import org.json.JSONException;
import org.json.JSONObject;
import org.micromanager.utils.ImageUtils;
import org.micromanager.utils.MDUtils;

import ij.IJ;
import ij.ImagePlus;

public class IndividualImagesHandler implements AcqOutputHandler {
	private File outputDirectory;
	private String namingScheme;
	
	/**
	 * Creates a nameScheme string from a list of 'short' names. Only applies to
	 * saveIndividually.
	 * 
	 * @param header Prefixed onto the string.
	 * @param t	whether or not to include time in filename
	 * @param nameMap map of short names for devices to be in the filename.
	 * @return the generated scheme (is also saved to this object!)
	 */
	public static String shortNamesToScheme(String header, boolean xyztt[], String[] nameMap) {
		String nameScheme = header;

		if(xyztt[4])
			nameScheme += "-t=$(dt)";
		if(xyztt[0])
			nameScheme += "-" + (nameMap == null ? "X" : nameMap[0]) + "=$(X)";
		if(xyztt[1])
			nameScheme += "-" + (nameMap == null ? "Y" : nameMap[1]) + "=$(Y)";

		if(xyztt[2])
			nameScheme += "-" + (nameMap == null ? "Z" : nameMap[2]) + "=$(Z)";
		
		if(xyztt[3])
			nameScheme += "-" + (nameMap == null ? "Theta" : nameMap[3]) + "=$(T)";

		nameScheme += ".tif";

		return nameScheme;
	}

	public IndividualImagesHandler(File directory, String scheme) {
		outputDirectory = directory;
		if(!outputDirectory.exists() || !outputDirectory.isDirectory())
			throw new IllegalArgumentException("Invalid path or not a directory: " + directory.getAbsolutePath());

		namingScheme = scheme;
	}

	@Override
	public void processSlice(TaggedImage img) throws Exception {
		String name = nameImage(img.tags);

		ImagePlus imp = new ImagePlus(name, ImageUtils.makeProcessor(img));
		imp.setProperty("Info", img.tags.toString(1));

		IJ.save(imp, new File(outputDirectory, name).getAbsolutePath());
	}

	private String nameImage(JSONObject tags) throws JSONException {
		String result = namingScheme
			.replace("$(TP)", Integer.toString(MDUtils.getFrameIndex(tags)))
			.replace("$(A)", Integer.toString(MDUtils.getPositionIndex(tags)))
			.replace("$(X)", Double.toString(MDUtils.getXPositionUm(tags)))
			.replace("$(Y)", Double.toString(MDUtils.getYPositionUm(tags)))
			.replace("$(Z)", Double.toString(MDUtils.getZPositionUm(tags)))
			.replace("$(T)", Double.toString(tags.getDouble(ProgrammaticAcquisitor.THETA_POSITION_TAG)))
			.replace("$(dt)", Double.toString(MDUtils.getElapsedTimeMs(tags)));

		Iterator<String> keys = tags.keys();
		while(keys.hasNext())
		{
			String key = keys.next();
			result = result.replace("${" + key + "}", tags.optString(key));
		}

		return result;
	}

	@Override
	public void finalizeAcquisition() throws Exception {
		// Nothing to do.
	}

	@Override
	public ImagePlus getImagePlus() throws Exception {
		IJ.run("QuickPALM.Run_MyMacro", "Fast_VirtualStack_Opener.txt");

		return IJ.getImage();
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

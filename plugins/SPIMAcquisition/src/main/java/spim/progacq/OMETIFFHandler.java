package spim.progacq;

import java.io.File;
import java.util.UUID;

import ij.IJ;
import ij.ImagePlus;
import ij.process.ImageProcessor;

import org.micromanager.utils.ReportingUtils;

import loci.common.DataTools;
import loci.common.services.ServiceFactory;
import loci.formats.FormatTools;
import loci.formats.IFormatWriter;
import loci.formats.ImageWriter;
import loci.formats.MetadataTools;
import loci.formats.meta.IMetadata;
import loci.formats.services.OMEXMLService;
import mmcorej.CMMCore;
import ome.xml.model.enums.DimensionOrder;
import ome.xml.model.enums.PixelType;
import ome.xml.model.primitives.NonNegativeInteger;
import ome.xml.model.primitives.PositiveFloat;
import ome.xml.model.primitives.PositiveInteger;
import spim.setup.SPIMSetup;
import spim.setup.SPIMSetup.SPIMDevice;

public class OMETIFFHandler implements AcqOutputHandler {
	private File outputDirectory;

	private IMetadata meta;
	private IFormatWriter writer;

	private CMMCore core;
	private int stacks, timesteps;
	private int planeCounter;
	private double deltat;

	public OMETIFFHandler(SPIMSetup setup, File outDir, AcqRow[] acqRows,
			int iTimeSteps, double iDeltaT) {

		if(outDir == null || !outDir.exists() || !outDir.isDirectory())
			throw new IllegalArgumentException("Null path specified: " + outDir.toString());

		stacks = acqRows.length;
		core = setup.getCore();
		timesteps = iTimeSteps;
		deltat = iDeltaT;
		outputDirectory = outDir;

		planeCounter = 0;

		try {
			meta = new ServiceFactory().getInstance(OMEXMLService.class).createOMEXMLMetadata();

			meta.createRoot();
			meta.setDatasetID(MetadataTools.createLSID("Dataset", 0), 0);
			meta.setInstrumentID("OpenSPIM", 0);

			for(SPIMDevice dev : acqRows[0].getDevices())
			{
				switch(dev)
				{
				case CAMERA1:
					meta.setDetectorID(setup.getDevice(dev).getDeviceName(), 0, 0);
					break;
				case CAMERA2:
					meta.setDetectorID(setup.getDevice(dev).getDeviceName(), 0, 1);
					break;
				case LASER1:
					meta.setLaserID(setup.getDevice(dev).getDeviceName(), 0, 0);
					break;
				case LASER2:
					meta.setLaserID(setup.getDevice(dev).getDeviceName(), 0, 1);
					break;
				case STAGE_THETA:
				case STAGE_X:
				case STAGE_Y:
				case STAGE_Z:
				case SYNCHRONIZER:
					break;
				default:
					break;
				}
			}

			for (int t = 0; t < timesteps; ++t) {
				for (int view = 0; view < stacks; ++view) {
					AcqRow row = acqRows[view];
					int depth = row.getDepth();

					int image = image(t, view);

					meta.setImageID(MetadataTools.createLSID("Image", view, t), image);

					meta.setPixelsID(MetadataTools.createLSID("Pixels", 0), image);
					meta.setPixelsBigEndian(Boolean.TRUE, image);
					meta.setPixelsDimensionOrder(DimensionOrder.XYCZT, image);
					meta.setPixelsType(PixelType.fromString(FormatTools.getPixelTypeString(FormatTools.pixelTypeFromBytes((int) core.getBytesPerPixel(), false, false))), image);
					meta.setPixelsSizeX(new PositiveInteger((int) core.getImageWidth()), image);
					meta.setPixelsSizeY(new PositiveInteger((int) core.getImageHeight()), image);
					meta.setPixelsSizeZ(new PositiveInteger(depth), image);
					meta.setPixelsSizeC(new PositiveInteger(1), image);
					meta.setPixelsSizeT(new PositiveInteger(1), image);
					meta.setPixelsPhysicalSizeX(new PositiveFloat(core.getPixelSizeUm()), image);
					meta.setPixelsPhysicalSizeY(new PositiveFloat(core.getPixelSizeUm()), image);
					meta.setPixelsPhysicalSizeZ(new PositiveFloat(Math.max(row.getZStepSize(), 0.1D)), image);
					meta.setPixelsTimeIncrement(new Double(deltat), image);
					meta.setPixelsSignificantBits(new PositiveInteger((int) core.getImageBitDepth()), image);

					meta.setChannelID(MetadataTools.createLSID("Channel", 0), image, 0);
					meta.setChannelSamplesPerPixel(new PositiveInteger(1), image, 0);

					meta.setTiffDataFirstC(new NonNegativeInteger(0), image, 0);
					meta.setTiffDataFirstZ(new NonNegativeInteger(0), image, 0);
					meta.setTiffDataFirstT(new NonNegativeInteger(t), image, 0);
					meta.setTiffDataPlaneCount(new NonNegativeInteger(1), image, 0); // Needs to get updated later.

					String fileName = makeFilename(view, t);
					meta.setUUIDFileName(fileName, image, 0);
					meta.setUUIDValue(UUID.randomUUID().toString(), view, 0);
				}
			}

			writer = new ImageWriter().getWriter(makeFilename(0, 0));

			writer.setWriteSequentially(true);
			writer.setMetadataRetrieve(meta);
			writer.setInterleaved(false);
			writer.setValidBitsPerPixel((int) core.getImageBitDepth());
			writer.setCompression("Uncompressed");
		} catch(Throwable t) {
			IJ.handleException(t);
			throw new IllegalArgumentException(t);
		}
	}

	private static String makeFilename(int view, int timepoint) {
		return String.format("spim_TL%02d_Angle%01d.ome.tiff", (timepoint + 1), view);
	}

	private int image(int timepoint, int view) {
		return timepoint*stacks + view;
	}

	private void openWriter(int view, int timepoint) throws Exception {
		int image = image(timepoint, view);
		writer.changeOutputFile(new File(outputDirectory, meta.getUUIDFileName(image, 0)).getAbsolutePath());
		writer.setSeries(image);
		meta.setUUID(meta.getUUIDValue(image, 0));
	}

	@Override
	public ImagePlus getImagePlus() throws Exception {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public void beginStack(int timepoint, int view) throws Exception {
		ReportingUtils.logMessage("Beginning stack for timepoint " + timepoint + ", view " + view);

		openWriter(view, timepoint);
		planeCounter = 0;
	}

	private int doubleAnnotations = 0;
	private int storeDouble(int image, int plane, int n, String name, double val) {
		String key = String.format("%d/%d/%d: %s", image, plane, n, name);

		meta.setDoubleAnnotationID(key, doubleAnnotations);
		meta.setDoubleAnnotationValue(val, doubleAnnotations);
		meta.setPlaneAnnotationRef(key, image, plane, n);

		return doubleAnnotations++;
	}

	@Override
	public void processSlice(int timepoint, int view, ImageProcessor ip, double X, double Y, double Z, double theta, double deltaT)
			throws Exception {
		byte[] data = null;

		switch(ip.getBitDepth()) {
		case 8:
			data = (byte[])ip.getPixels();
			break;
		case 16:
			data = DataTools.shortsToBytes((short[])ip.getPixels(), true);
			break;
		case 32:
			data = (ip instanceof ij.process.FloatProcessor) ? DataTools.floatsToBytes((float[])ip.getPixels(), true) : DataTools.intsToBytes((int[])ip.getPixels(), true);
			break;
		case 64:
			data = DataTools.longsToBytes((long[])ip.getPixels(), true);
			break;
		}

		int image = image(timepoint, view);
		int plane = planeCounter;

		meta.setPlanePositionX(X, image, plane);
		meta.setPlanePositionY(Y, image, plane);
		meta.setPlanePositionZ(Z, image, plane);
		meta.setPlaneDeltaT(deltaT, image, plane);

		meta.setPlaneTheC(new NonNegativeInteger(0), image, plane);
		meta.setPlaneTheZ(new NonNegativeInteger(planeCounter), image, plane);
		meta.setPlaneTheT(new NonNegativeInteger(timepoint), image, plane);

		storeDouble(image, plane, 0, "Theta", theta);

		try {
			writer.saveBytes(plane, data);
		} catch(java.io.IOException ioe) {
			finalizeStack(timepoint, view);
			if(writer != null)
				writer.close();
			throw new Exception("Error writing OME-TIFF.", ioe);
		}

		++planeCounter;
	}

	@Override
	public void finalizeStack(int timepoint, int view) throws Exception {
		ReportingUtils.logMessage("Finished stack of view " + view + " at timepoint " + timepoint);

		meta.setPixelsSizeZ(new PositiveInteger(planeCounter), image(timepoint, view));
	}

	@Override
	public void finalizeAcquisition() throws Exception {
		if(writer != null)
			writer.close();

		writer = null;
	}
}

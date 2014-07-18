package spim.setup;

import java.util.Arrays;

import mmcorej.CMMCore;
import spim.setup.SPIMSetup.SPIMDevice;

public class PicardStage extends Stage {
	/*
	 * Every new device implementation needs to define a Factory to create it.
	 * This lets you implement new devices with only the addition of a single
	 * file. This is done using Device.installFactory.
	 * 
	 * The 'manufacture' method is where you make a new instance of your device.
	 * If the class needs any special setup beyond the constructor, be sure to
	 * do that before returning. Here, we only need to make a new PicardStage.
	 * 
	 * The string following the Factory is the device name (*not* label!) that
	 * this factory can create. After that comes the SPIMDevices that this class
	 * can control. You can list as many as you want; as seen here, the X, Y,
	 * and Z stages could each be a linear stage.
	 * 
	 * Finally, your factory class name(s) should be in a service-loader list
	 * in <jar>/META-INF/services/spim.setup.Device$Factory.
	 */
	public static class Factory implements Device.Factory {
		@Override
		public String deviceName() {
			return "Picard Z Stage";
		}

		@Override
		public Iterable<SPIMDevice> deviceTypes() {
			return Arrays.asList(SPIMDevice.STAGE_X, SPIMDevice.STAGE_Y, SPIMDevice.STAGE_Z);
		}

		public Device manufacture(CMMCore core, String label) {
			return new PicardStage(core, label);
		}
	}

	public PicardStage(CMMCore core, String label) {
		super(core, label);
	}

	@Override
	public void home() {
		// TODO: This is a workaround, since the MM API lacks a home() call for single-axis stages.
		setProperty("GoHome", 1);
		waitFor();
	}
}

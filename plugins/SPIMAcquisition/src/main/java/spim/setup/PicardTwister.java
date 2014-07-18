package spim.setup;

import java.util.Arrays;

import mmcorej.CMMCore;
import spim.setup.SPIMSetup.SPIMDevice;

public class PicardTwister extends GenericRotator {
	public static class Factory implements Device.Factory {
		@Override
		public String deviceName() {
			return "Picard Twister";
		}

		@Override
		public Iterable<SPIMDevice> deviceTypes() {
			return Arrays.asList(SPIMDevice.STAGE_THETA);
		}

		@Override
		public Device manufacture(CMMCore core, String label) {
			return new PicardTwister(core, label);
		}
	}

	public PicardTwister(CMMCore core, String label) {
		super(core, label);
	}
}

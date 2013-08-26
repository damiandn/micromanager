package spim.setup;

import mmcorej.CMMCore;
import spim.setup.SPIMSetup.SPIMDevice;

public class PicardTwister extends GenericRotator {
	static {
		Device.installFactory(new Factory() {
			@Override
			public Device manufacture(CMMCore core, String label) {
				return new PicardTwister(core, label);
			}
		}, "Picard Twister", SPIMDevice.STAGE_THETA);
	}

	public PicardTwister(CMMCore core, String label) {
		super(core, label);
	}
}

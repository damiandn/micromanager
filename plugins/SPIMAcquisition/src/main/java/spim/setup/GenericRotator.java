package spim.setup;

import java.util.Arrays;
import java.util.Collection;

import spim.setup.SPIMSetup.SPIMDevice;

import mmcorej.CMMCore;

public class GenericRotator extends Stage {
	static {
		Device.installFactory(new Factory() {
			@Override
			public Device manufacture(CMMCore core, String label) {
				return new GenericRotator(core, label);
			}
		}, "*", SPIMDevice.STAGE_THETA);
	}

	public GenericRotator(CMMCore core, String label) {
		super(core, label);
	}

	@Override
	public void setPosition(double position) {
		super.setPosition(super.getPosition() + reduce(reduce(position) - reduce(super.getPosition())));
	}

	@Override
	public double getPosition() {
		return reduce(super.getPosition());
	}

	private static double reduce(double r) {
		double s = r > 0.0D ? 1.0D : -1.0D;

		return ((r + s*180.0D) % 360.0D) - s*180.0D;
	}

	@Override
	public double getMinPosition() {
		return -180.0D;
	}

	@Override
	public double getMaxPosition() {
		return +180.0D;
	}
}

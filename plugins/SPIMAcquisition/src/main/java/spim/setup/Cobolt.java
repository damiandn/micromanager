package spim.setup;

import mmcorej.CMMCore;
import spim.setup.SPIMSetup.SPIMDevice;

public class Cobolt extends Laser {
	static {
		Device.installFactory(new Factory() {
			@Override
			public Device manufacture(CMMCore core, String label) {
				return new Cobolt(core, label);
			}
		}, "Cobolt", SPIMDevice.LASER1, SPIMDevice.LASER2);
	}

	public Cobolt(CMMCore core, String label) {
		super(core, label);
	}

	@Override
	public void setPower(double power) {
		setProperty("PowerSetpoint", power * 1000);
	}

	@Override
	public double getPower() {
		return getPropertyDouble("PowerSetpoint") / 1000;
	}

	@Override
	public double getMinPower() {
		return getPropertyDouble("Minimum Laser Power") / 1000.0;
	}

	@Override
	public double getMaxPower() {
		return getPropertyDouble("Maximum Laser Power") / 1000.0;
	}
}

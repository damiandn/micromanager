package spim.progacq;

import java.text.NumberFormat;
import java.text.ParseException;
import java.util.EnumMap;

import org.apache.commons.math3.geometry.euclidean.threed.Vector3D;

import spim.setup.SPIMSetup.SPIMDevice;

public class AcqRow {
	public static class ValueSet {
		@Deprecated
		public static ValueSet fromString(String in) throws ParseException {
			NumberFormat parser = NumberFormat.getInstance();

			if(in.indexOf('-') > 0 && in.indexOf('@') > 0) {
				int dash = in.indexOf('-');
				int at = in.indexOf('@');

				return new ValueSet(
						parser.parse(in.substring(0, dash)).doubleValue(),
						parser.parse(in.substring(dash + 1, at)).doubleValue(),
						parser.parse(in.substring(at + 1)).doubleValue(),
						true
				);
			} else if(in.indexOf(':') > 0) {
				int first = in.indexOf(':');
				int last = in.lastIndexOf(':');

				return new ValueSet(
						parser.parse(in.substring(0, first)).doubleValue(),
						parser.parse(in.substring(last + 1)).doubleValue(),
						parser.parse(in.substring(first + 1, last)).doubleValue(),
						false
				);
			} else if(in.indexOf(';') > 0) {
				String[] substrs = in.split(";");
				double[] points = new double[substrs.length];

				for(int i = 0; i < substrs.length; ++i)
					points[i] = parser.parse(substrs[i]).doubleValue();

				return new ValueSet(points);
			} else {
				return new ValueSet(parser.parse(in).doubleValue());
			}
		}

		private final double stepSize;
		private final double continuousSpeed;
		private final double[] steps;

		public ValueSet(double single) {
			continuousSpeed = stepSize = -1;
			steps = new double[] { single };
		}

		public ValueSet(double[] points) {
			if(points.length < 1)
				throw new IllegalArgumentException("DeviceValueSet requires at least one position.");

			continuousSpeed = stepSize = -1;
			steps = points;
		}

		public ValueSet(double start, double end, double stepOrSpeed, boolean continuous) {
			if(continuous) {
				stepSize = -1;
				continuousSpeed = stepOrSpeed;
				steps = new double[] { start, end };
			} else {
				stepSize = stepOrSpeed;
				continuousSpeed = -1;

				steps = new double[(int) ((end - start) / stepOrSpeed + 0.5)];
				steps[0] = start;
				for(int i = 1; i < steps.length - 1; ++i)
					steps[i] = start + stepOrSpeed*i;
				steps[steps.length - 1] = end;
			}
		}

		public double getStartPosition() {
			return steps[0];
		}

		public double getEndPosition() {
			return steps[steps.length - 1];
		}

		public double getStepSize() {
			return stepSize;
		}

		public double getSpeed() {
			return continuousSpeed;
		}

		public int getSteps() {
			if(continuousSpeed > 0)
				return 999;

			return steps.length;
		}

		public double[] values() {
			return steps;
		}

		@Override
		public String toString() {
			if(steps.length == 1)
				return String.format("%.3f", steps[0]);

			if(continuousSpeed > 0)
				return String.format("%.3f-%.3f@%.1f", steps[0], steps[1], continuousSpeed);
			else if(stepSize > 0)
				return String.format("%.3f:%.3f:%.3f", steps[0], stepSize, steps[steps.length - 1]);
			else
				return steps.toString().replace(", ", ";").substring(1).replace("]","").trim();
		}

		protected void translate(double by) {
			for(int i = 0; i < steps.length; ++i)
				steps[i] += by;
		}
	}

	private EnumMap<SPIMDevice, ValueSet> posMap;

	protected AcqRow() {
		posMap = new EnumMap<SPIMDevice, ValueSet>(SPIMDevice.class);
	}

	public AcqRow(SPIMDevice[] devs, String[] infos) throws ParseException {
		this();

		for(int i = 0; i < devs.length; ++i)
			setValueSet(devs[i], ValueSet.fromString(infos[i]));
	}

	public AcqRow(double x, double y, double z, double t) {
		this();

		posMap.put(SPIMDevice.STAGE_X, new ValueSet(x));
		posMap.put(SPIMDevice.STAGE_Y, new ValueSet(y));
		posMap.put(SPIMDevice.STAGE_Z, new ValueSet(z));
		posMap.put(SPIMDevice.STAGE_THETA, new ValueSet(t));
	}

	public AcqRow(double x, double y, double zstart, double zend, double zstepspeed, boolean continuous, double t) {
		this();

		posMap.put(SPIMDevice.STAGE_X, new ValueSet(x));
		posMap.put(SPIMDevice.STAGE_Y, new ValueSet(y));
		posMap.put(SPIMDevice.STAGE_Z, new ValueSet(zstart, zend, zstepspeed, continuous));
		posMap.put(SPIMDevice.STAGE_THETA, new ValueSet(t));
	}

	public AcqRow(SPIMDevice[] devs, ValueSet[] vals) {
		this();

		for(int i = 0; i < devs.length; ++i)
			posMap.put(devs[i], vals[i]);
	}

	public void setValueSet(SPIMDevice dev, ValueSet set) {
		if(set == null)
			posMap.remove(dev);
		else
			posMap.put(dev, set);
	}
	
	public ValueSet getValueSet(SPIMDevice dev) {
		return posMap.get(dev);
	}
	
	public SPIMDevice[] getDevices() {
		return posMap.keySet().toArray(new SPIMDevice[posMap.size()]);
	}

	public String describeValueSet(SPIMDevice dev) {
		return posMap.get(dev).toString();
	}

	public double getZStartPosition() {
		return posMap.get(SPIMDevice.STAGE_Z).getStartPosition();
	}

	public double getZEndPosition() {
		return posMap.get(SPIMDevice.STAGE_Z).getEndPosition();
	}

	public double getZVelocity() {
		return posMap.get(SPIMDevice.STAGE_Z).getSpeed();
	}

	public double getZStepSize() {
		return posMap.get(SPIMDevice.STAGE_Z).getStepSize();
	}

	public boolean getZContinuous() {
		return posMap.get(SPIMDevice.STAGE_Z).getSpeed() > 0;
	}

	public int getDepth() {
		int out = 1;

		for(ValueSet set : posMap.values())
			out *= set.getSteps();

		return out;
	}

	public double getX() {
		return posMap.get(SPIMDevice.STAGE_X).getStartPosition();
	}

	public double getY() {
		return posMap.get(SPIMDevice.STAGE_Y).getStartPosition();
	}
	
	public double getTheta() {
		return posMap.get(SPIMDevice.STAGE_THETA).getStartPosition();
	}

	public void translate(Vector3D v) {
		posMap.get(SPIMDevice.STAGE_X).translate(v.getX());
		posMap.get(SPIMDevice.STAGE_Y).translate(v.getY());
		posMap.get(SPIMDevice.STAGE_Z).translate(v.getZ());
	}
}

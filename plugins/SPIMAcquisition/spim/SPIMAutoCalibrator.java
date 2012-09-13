package spim;

import ij.ImagePlus;
import ij.ImageStack;
import ij.process.ImageProcessor;

import java.awt.Component;
import java.awt.GridLayout;
import java.awt.Rectangle;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.util.LinkedList;
import java.util.List;

import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.DefaultListModel;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JList;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSpinner;
import javax.swing.JTable;
import javax.swing.SpinnerNumberModel;

import mmcorej.CMMCore;
import mmcorej.TaggedImage;

import org.apache.commons.math.geometry.euclidean.threed.Rotation;
import org.apache.commons.math.geometry.euclidean.threed.Vector3D;
import org.apache.commons.math.geometry.euclidean.threed.Line;
import org.json.JSONException;
import org.micromanager.MMStudioMainFrame;
import org.micromanager.utils.MDUtils;
import org.micromanager.utils.MMScriptException;
import org.micromanager.utils.ReportingUtils;

import edu.valelab.GaussianFit.GaussianFit;

public class SPIMAutoCalibrator extends JFrame implements SPIMCalibrator, ActionListener {
	private static final String ZMODE_MIN_SIGMA = "Min Sigma";
	private static final String ZMODE_MAX_INTENSITY = "Max Intens.";
	private static final String BTN_OBTAIN_NEXT = "Obtain Next";
	private static final String BTN_REVISE = "Revise Sel.";
	private static final String BTN_REMOVE = "Remove Sel.";
	private static final String BTN_RECALCULATE = "Recalculate";
	private static final String BTN_REVERSE = "Reverse Axis";

	private static final long serialVersionUID = -2162347623413462344L;

	private Line rotAxis;

	private CMMCore core;
	private MMStudioMainFrame gui;
	private String twisterLabel;

	private JList pointsTable;

	private JLabel rotAxisLbl;

	private JLabel rotOrigLbl;
	
	private JFrame tweaksFrame;
	private JSpinner firstDelta;
	private JSpinner secondDelta;
	private JComboBox zmethod;
	private JCheckBox complexGuessZ;
	private JSpinner intbgrThresh;

	public SPIMAutoCalibrator(CMMCore core, MMStudioMainFrame gui, String itwister) {
		super("SPIM Automatic Calibration");

		this.core = core;
		this.gui = gui;
		this.twisterLabel = itwister;

		rotAxis = null;

		JButton go = new JButton(BTN_OBTAIN_NEXT);
		go.addActionListener(this);

		JButton revise = new JButton(BTN_REVISE);
		revise.addActionListener(this);

		JButton remove = new JButton(BTN_REMOVE);
		remove.addActionListener(this);

		JButton recalculate = new JButton(BTN_RECALCULATE);
		recalculate.addActionListener(this);

		JButton revert = new JButton(BTN_REVERSE);
		revert.addActionListener(this);

		this.getContentPane().setLayout(new BoxLayout(this.getContentPane(), BoxLayout.PAGE_AXIS));

		JPanel btnsPanel = new JPanel();
		btnsPanel.setLayout(new GridLayout(6, 1));

		JButton tweakScan = new JButton("Tweak Scan");
		tweakScan.addActionListener(this);

		LayoutUtils.addAll(btnsPanel,
				go,
				revise,
				remove,
				recalculate,
				revert,
				tweakScan
		);

		btnsPanel.setMaximumSize(btnsPanel.getPreferredSize());

		add(LayoutUtils.horizPanel(
			new JScrollPane(pointsTable = new JList(new DefaultListModel())),
			btnsPanel,
			Box.createVerticalGlue()
		));

		add(LayoutUtils.horizPanel(
			LayoutUtils.titled("Calculated Values", (JComponent) LayoutUtils.vertPanel(
				rotAxisLbl = new JLabel("Rotational axis: "),
				rotOrigLbl = new JLabel("Rot. axis origin: ")
			)),
			Box.createHorizontalGlue()
		));

		pack();

		tweaksFrame = new JFrame("Scanning Tweaks");
		tweaksFrame.setLayout(new GridLayout(5, 1));

		LayoutUtils.addAll((JComponent) tweaksFrame.getContentPane(),
			LayoutUtils.horizPanel(
				new JLabel("First delta:"),
				firstDelta = new JSpinner(new SpinnerNumberModel(5.0, 1.0, 30.0, 1.0))
			),
			LayoutUtils.horizPanel(
				new JLabel("Second delta:"),
				secondDelta = new JSpinner(new SpinnerNumberModel(15.0, 5.0, 50.0, 1.0))
			),
			zmethod = new JComboBox(new String[] {"Weighted Avg.", ZMODE_MAX_INTENSITY, ZMODE_MIN_SIGMA}),
			complexGuessZ = new JCheckBox("Complex Z Guessing"),
			LayoutUtils.horizPanel(
				new JLabel("IntBGR:"),
				intbgrThresh = new JSpinner(new SpinnerNumberModel(0.10, 0.0, 0.5, 0.01))
			)
		);

		tweaksFrame.pack();
	}

	@Override
	public double getUmPerPixel() {
		// TODO Auto-generated method stub
		return 0.43478260869565217391304347826087;
	}

	@Override
	public Vector3D getRotationOrigin() {
		// TODO Auto-generated method stub
		return rotAxis != null ? rotAxis.getOrigin() : null;
	}

	@Override
	public Vector3D getRotationAxis() {
		// TODO Auto-generated method stub
		return rotAxis != null ? rotAxis.getDirection() : null;
	}

	@Override
	public boolean getIsCalibrated() {
		// TODO Auto-generated method stub
		return getUmPerPixel() != 0 && rotAxis != null;
	}

	private Line fitAxis() {
		Object[] vectors = ((DefaultListModel)pointsTable.getModel()).toArray();
		LinkedList<double[]> doublePoints = new LinkedList<double[]>();

		for(Object vec : vectors) {
			Vector3D v = (Vector3D)vec;
			doublePoints.add(new double[] {v.getX(), v.getY(), v.getZ()});
		}

		FitHypersphere circle = new FitHypersphere(doublePoints);

		double[] center = circle.getCenter();

		Vector3D axisPoint = new Vector3D(center[0], center[1], center[2]);

		return new Line(axisPoint, axisPoint.add(Vector3D.PLUS_J));
	};

	private static double minIntBGR = 0.05;

	private double guessZ() throws Exception {
		int modelSize = pointsTable.getModel().getSize();

		if(modelSize >= 3 && complexGuessZ.isSelected()) {
			Line a = fitAxis();
			
			Vector3D cur = new Vector3D(
					core.getXPosition(core.getXYStageDevice()),
					core.getYPosition(core.getXYStageDevice()),
					core.getPosition(core.getFocusDevice())
				);
			
			Vector3D res = new Rotation(a.getDirection(),
					-1*(Math.PI/100)).applyTo(cur);
			
			return res.getZ();
		} else if(modelSize >= 2) {
			Vector3D recent = (Vector3D)pointsTable.getModel().getElementAt(modelSize - 1);
			Vector3D older = (Vector3D)pointsTable.getModel().getElementAt(modelSize - 2);

			return recent.getZ() + (recent.getZ() - older.getZ());
		} else {
			return core.getPosition(core.getFocusDevice());
		}
	}

	private Vector3D scanBead(double scanDelta) throws Exception {
		double basez = guessZ();

		GaussianFit fitter = new GaussianFit(3, 1);

		double cx = 0, cy = 0, cz = 0, intsum = 0;

		double maxInt = -1e6;
		double maxZ = -1;
		
		double minSigma = 1e6;
		double bestZ = -1;

		ReportingUtils.logMessage(String.format("!!!--- SCANNING %.2f to %.2f", basez - scanDelta, basez + scanDelta));

		Rectangle roi = MMStudioMainFrame.getSimpleDisplay().getImagePlus().getProcessor().getRoi();
		ImageStack stack = new ImageStack(roi.width, roi.height);

		for(double z = basez - scanDelta; z <= basez + scanDelta; ++z) {
			core.setPosition(core.getFocusDevice(), z);
			core.waitForDevice(core.getFocusDevice());
			Thread.sleep(15);

			core.snapImage();
			TaggedImage ti = core.getTaggedImage();
			addTags(ti, 0);
			gui.addImage(MMStudioMainFrame.SIMPLE_ACQ, ti, true, true);

			MMStudioMainFrame.getSimpleDisplay().updateAndDraw();

			ImageProcessor ip = MMStudioMainFrame.getSimpleDisplay().getImagePlus().getProcessor();

			ImageProcessor cropped = ip.crop();

			stack.addSlice(cropped);

			double[] params = fitter.doGaussianFit(cropped, Integer.MAX_VALUE);
			double intbgr = params[GaussianFit.INT] / params[GaussianFit.BGR];

			ReportingUtils.logMessage(String.format("!!!--- Gaussian fit: C=%.2f, %.2f, INT=%.2f, BGR=%.2f.",
					params[GaussianFit.XC], params[GaussianFit.YC], params[GaussianFit.INT], params[GaussianFit.BGR]));

			double offsx = core.getXPosition(core.getXYStageDevice()) +
					(ip.getRoi().getMinX() + params[GaussianFit.XC] -
					ip.getWidth()/2)*getUmPerPixel();

			double offsy = core.getYPosition(core.getXYStageDevice()) +
					(ip.getRoi().getMinY() + params[GaussianFit.YC] -
					ip.getHeight()/2)*getUmPerPixel();

			double sigma = Math.sqrt(Math.pow(params[GaussianFit.S1], 2) +
				Math.pow(params[GaussianFit.S2], 2));

			if(intbgr >= (Double)intbgrThresh.getValue() &&
					params[GaussianFit.XC] >= 0 && params[GaussianFit.YC] >= 0 &&
					params[GaussianFit.XC] < cropped.getWidth() && params[GaussianFit.YC] < cropped.getHeight()) {
				intsum += intbgr;

				ReportingUtils.logMessage("!!!--- Including z=" + z + " (" + core.getPosition(core.getFocusDevice()) + "): " + offsx + ", " + offsy + ":");
				ReportingUtils.logMessage(core.getXPosition(core.getXYStageDevice()) + " + (" + ip.getRoi().getMinX() + " + " + params[GaussianFit.XC] + " - " + ip.getWidth() + "/2)*" + getUmPerPixel() + ")*" + intbgr + ";");
				ReportingUtils.logMessage(core.getYPosition(core.getXYStageDevice()) + " + (" + ip.getRoi().getMinY() + " + " + params[GaussianFit.YC] + " - " + ip.getHeight() + "/2)*" + getUmPerPixel() + ")*" + intbgr + ";");

				cx += offsx*intbgr;
				cy += offsy*intbgr;
				cz += core.getPosition(core.getFocusDevice())*intbgr;

				if(intbgr > maxInt) {
					maxInt = intbgr;
					maxZ = z;
				}

				if(sigma < minSigma) {
					minSigma = sigma;
					bestZ = z;
				}
			} else {
				ReportingUtils.logMessage("!!!--- Throwing out " + offsx + ", " + offsy + ", " + z + "; intbgr = " + intbgr);
			}
		}

//		(new ImagePlus(basez + "+/-" + scanDelta, stack)).show();
		core.setPosition(core.getFocusDevice(), basez);
		core.waitForDevice(core.getFocusDevice());

		cx /= intsum;
		cy /= intsum;
		cz /= intsum;

		if(zmethod.getSelectedItem().equals(ZMODE_MAX_INTENSITY)) {
			cz = maxZ;
		} else if(zmethod.getSelectedItem().equals(ZMODE_MIN_SIGMA)) {
			cz = bestZ;
		}

		Vector3D ret = new Vector3D(cx, cy, cz);

		ReportingUtils.logMessage("!!!--- RETURNING " + vToS(ret));

		return ret;
	};

	private void addTags(TaggedImage ti, int channel) throws JSONException {
		MDUtils.setChannelIndex(ti.tags, channel);
		MDUtils.setFrameIndex(ti.tags, 0);
		MDUtils.setPositionIndex(ti.tags, 0);
		MDUtils.setSliceIndex(ti.tags, 0);
		try {
			ti.tags.put("Summary", MMStudioMainFrame.getInstance().getAcquisition(MMStudioMainFrame.SIMPLE_ACQ).getSummaryMetadata());
		} catch (MMScriptException ex) {
			ReportingUtils.logError("Error adding summary metadata to tags");
		}
		gui.addStagePositionToTags(ti);
	}

	private boolean getNextBead() throws Exception {
		Vector3D next = scanBead((Double)firstDelta.getValue());

		if(next.isNaN())
			next = scanBead((Double)secondDelta.getValue());

		if(next.isNaN())
			return false;

		try {
			core.setXYPosition(core.getXYStageDevice(), next.getX(), next.getY());
			core.setPosition(core.getFocusDevice(), next.getZ());
		} catch(Exception e) {
			ReportingUtils.logError(e);
			JOptionPane.showMessageDialog(this, "Couldn't recenter: " + e.getMessage());
			return false;
		};

		((DefaultListModel)pointsTable.getModel()).addElement(next);

		ImageProcessor ip = gui.getImageWin().getImagePlus().getProcessor();

		Rectangle roi = ip.getRoi();

		roi.setLocation((ip.getWidth() - roi.width) / 2,
				(ip.getHeight() - roi.height) / 2);

		gui.getImageWin().getImagePlus().setRoi(roi);

		return true;
	};

	private String vToS(Vector3D v) {
		return String.format("<%.3f, %.3f, %.3f>", v.getX(), v.getY(), v.getZ());
	}
	
	private Runnable getNextBeadRunnable = new Runnable() {
		@Override
		public void run() {
			boolean live = gui.getLiveMode(); 
			try {
				gui.enableLiveMode(false);

				core.setPosition(twisterLabel, (core.getPosition(twisterLabel) + 1));
				core.waitForDevice(twisterLabel);
				Thread.sleep(50);
				if(!getNextBead()) {
					JOptionPane.showMessageDialog(SPIMAutoCalibrator.this,
							"Most likely, the bead has been lost. D: Sorry! " +
							"Try moving the stage to the most recent position" +
							" in the list.");
				}
			} catch(Exception e) {
				JOptionPane.showMessageDialog(SPIMAutoCalibrator.this,
						"Couldn't scan Z: " + e.getMessage());
				ReportingUtils.logError(e);
			} finally {
				gui.enableLiveMode(live);
			}
		}
	};

	public void actionPerformed(ActionEvent ae) {
		if(BTN_OBTAIN_NEXT.equals(ae.getActionCommand())) {
			Thread t = new Thread(getNextBeadRunnable);
			
			t.start();
		} else if("Tweak Scan".equals(ae.getActionCommand())) {
			tweaksFrame.setVisible(true);
		} else if(BTN_REVISE.equals(ae.getActionCommand())) {
			DefaultListModel mdl = (DefaultListModel)pointsTable.getModel();

			try {
				mdl.set(pointsTable.getSelectedIndex(),
						new Vector3D(core.getXPosition(core.getXYStageDevice()),
								core.getYPosition(core.getXYStageDevice()),
								core.getPosition(core.getFocusDevice())));
			} catch(Exception e) {
				JOptionPane.showMessageDialog(this, "Couldn't fetch: " + e.getMessage());
			}
		} else if(BTN_REMOVE.equals(ae.getActionCommand())) {
			DefaultListModel mdl = (DefaultListModel)pointsTable.getModel();

			for(Object obj : pointsTable.getSelectedValues())
				mdl.removeElement(obj);

			pointsTable.clearSelection();
		} else if(BTN_RECALCULATE.equals(ae.getActionCommand())) {
			if(pointsTable.getModel().getSize() <= 2)
				return;

			rotAxis = fitAxis();

			rotAxisLbl.setText("Rotational axis: " + vToS(rotAxis.getDirection()));
			rotOrigLbl.setText("Rot. axis origin: " + vToS(rotAxis.getOrigin()));
		} else if(BTN_REVERSE.equals(ae.getActionCommand())) {
			rotAxis = rotAxis.revert();

			rotAxisLbl.setText("Rotational axis: " + vToS(rotAxis.getDirection()));
			rotOrigLbl.setText("Rot. axis origin: " + vToS(rotAxis.getOrigin()));
		}
	};
}

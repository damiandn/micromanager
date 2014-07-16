/**
 * 
 */
package spim.progacq;

import java.text.ParseException;
import java.util.Iterator;
import java.util.List;
import java.util.ArrayList;

import javax.swing.table.AbstractTableModel;

import spim.setup.SPIMSetup.SPIMDevice;

/**
 * @author Luke Stuyvenberg
 * 
 */
public class StepTableModel extends AbstractTableModel implements
		Iterable<AcqRow> {
	private static final long serialVersionUID = -7369997010095627461L;

	private SPIMDevice[] devices;
	private ArrayList<AcqRow> data;

	public StepTableModel(SPIMDevice... devices) {
		super();

		this.devices = devices;
		data = new ArrayList<AcqRow>();
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see javax.swing.table.TableModel#getRowCount()
	 */
	@Override
	public int getRowCount() {
		return data.size();
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see javax.swing.table.TableModel#getColumnCount()
	 */
	@Override
	public int getColumnCount() {
		return devices.length;
	}

	public SPIMDevice[] getColumns() {
		return devices;
	}

	public List<AcqRow> getRows() {
		return data;
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see javax.swing.table.TableModel#getValueAt(int, int)
	 */
	@Override
	public Object getValueAt(int rowIndex, int columnIndex) {
		return data.get(rowIndex).describeValueSet(devices[columnIndex]);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see java.lang.Iterable#iterator()
	 */
	@Override
	public Iterator<AcqRow> iterator() {
		return data.iterator();
	}

	public void insertRow(AcqRow row) {
		insertRow(data.size(), row);
	}

	public void insertRow(int idx, AcqRow row) {
		data.add(idx, row);
		this.fireTableDataChanged();
	}

	public void insertRow(String... values) throws ParseException {
		insertRow(data.size(), values);
	}
	
	public void insertRow(int index, String... values) throws ParseException {
		// TODO: Handle this more gracefully? Fail silently? Chop?
		if (values.length != devices.length)
			throw new Error("Wrong colum count, silly!");

		data.add(index, new AcqRow(devices, values));

		this.fireTableDataChanged();
	}

	public void removeRows(int[] rows) {
		for (int rowidx = 0; rowidx < rows.length; ++rowidx)
			data.remove(rows[rowidx] - rowidx);

		this.fireTableDataChanged();
	}

	@Override
	public String getColumnName(int columnIndex) {
		return devices[columnIndex].getText();
	}

	@Override
	public Class<?> getColumnClass(int columnIndex) {
		return String.class;
	}

	@Override
	public boolean isCellEditable(int rowIndex, int columnIndex) {
		return false;
	}

	@Override
	public void setValueAt(Object aValue, int rowIndex, int columnIndex) {
		throw new UnsupportedOperationException();
	}

	public int[] move(int[] idxs, int delta) {
		if(delta == 0)
			return idxs;

		// Make sure we don't go off the end of the array.
		if(idxs[idxs.length - 1] + delta >= data.size()) {
			delta = data.size() - idxs[idxs.length - 1] - 1;
		} else if(idxs[0] + delta < 0) {
			delta = -idxs[0];
		}

		// This is a bit abstruse, but it basically loops forward for delta < 0 and backward for delta > 0.
		for(int i = (delta < 0 ? 0 : (idxs.length - 1)); delta < 0 ? (i < idxs.length) : (i >= 0); i += (delta < 0 ? 1 : -1))
		{
			data.add(idxs[i] + delta, data.remove(idxs[i]));
			idxs[i] += delta;
		}

		this.fireTableDataChanged();

		return idxs;
	}
};

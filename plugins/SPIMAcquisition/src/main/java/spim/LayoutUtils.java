package spim;

import java.awt.Component;
import java.awt.Container;
import java.awt.Dimension;
import java.util.Arrays;
import java.util.Map;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.GroupLayout;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;

public class LayoutUtils {
	public static <T extends JComponent> T titled(String title, T c) {
		c.setBorder(BorderFactory.createTitledBorder(title));

		return c;
	}

	public static JPanel vertPanel(Component... parts) {
		return vertPanel(0, parts);
	}

	public static JPanel horizPanel(Component... parts) {
		return horizPanel(0, parts);
	}

	public static JPanel vertPanel(String title, Component... parts) {
		return vertPanel(title, 0, parts);
	}

	public static JPanel horizPanel(String title, Component... parts) {
		return horizPanel(title, 0, parts);
	};

	public static JPanel vertPanel(int gap, Component... parts) {
		JPanel panel = new JPanel();
		panel.setLayout(new BoxLayout(panel, BoxLayout.PAGE_AXIS));

		return addAll(panel, gap, parts);
	}

	public static JPanel horizPanel(int gap, Component... parts) {
		JPanel panel = new JPanel();
		panel.setLayout(new BoxLayout(panel, BoxLayout.LINE_AXIS));

		return addAll(panel, gap, parts);
	};

	public static JPanel vertPanel(String title, int gap, Component... parts) {
		JPanel panel = new JPanel();
		panel.setLayout(new BoxLayout(panel, BoxLayout.PAGE_AXIS));

		return addAll(titled(title, panel), gap, parts);
	}

	public static JPanel horizPanel(String title, int gap, Component... parts) {
		JPanel panel = new JPanel();
		panel.setLayout(new BoxLayout(panel, BoxLayout.LINE_AXIS));

		return addAll(titled(title, panel), gap, parts);
	};

	public static <T extends Container> T addAll(T to, Component... parts) {
		return addAll(to, 0, parts);
	}

	public static <T extends Container> T addAll(T to, int gap, Component... parts) {
		for(Component c : parts) {
			to.add(c);

			if(gap > 0)
				to.add(Box.createRigidArea(new Dimension(gap, gap)));
		}

		return to;
	}

	public static JPanel labelMe(Component c, String lbl) {
		return horizPanel(new JLabel(lbl), c);
	}

	public static JPanel form(Map<String, Component> labelCompPairs) {
		JPanel out = new JPanel();
		GroupLayout layout = new GroupLayout(out);
		out.setLayout(layout);

		layout.setAutoCreateGaps(true);
		layout.setAutoCreateContainerGaps(true);

		GroupLayout.Group horizontalLabelsGroup = layout.createParallelGroup(GroupLayout.Alignment.TRAILING);
		GroupLayout.Group horizontalComponentsGroup = layout.createParallelGroup(GroupLayout.Alignment.LEADING);
		GroupLayout.Group verticalGroup = layout.createSequentialGroup();

		for(Map.Entry<String, Component> formEntry : labelCompPairs.entrySet()) {
			JLabel label = new JLabel(formEntry.getKey());

			horizontalLabelsGroup.addComponent(label);
			horizontalComponentsGroup.addComponent(formEntry.getValue());

			verticalGroup.addGroup(layout.createParallelGroup(GroupLayout.Alignment.CENTER)
					.addComponent(label)
					.addComponent(formEntry.getValue())
			);
		}

		layout.setVerticalGroup(verticalGroup);
		layout.setHorizontalGroup(layout.createSequentialGroup()
				.addGroup(horizontalLabelsGroup)
				.addGroup(horizontalComponentsGroup)
		);

		return out;
	}

	public static JPanel form(int colpairs, Object... components) {
		Object[] newcomp = new Object[components.length + (2*colpairs) + components.length % (2*colpairs)];

		for(int i = 0; i < 2*colpairs; ++i)
			newcomp[i] = (i % 2 == 0) ? GroupLayout.Alignment.TRAILING : GroupLayout.Alignment.LEADING;

		for(int i = 0; i < components.length; ++i)
			newcomp[2*colpairs + i] = (components[i] == null) ? Box.createGlue() : ((components[i] instanceof Component) ? (Component) components[i] : new JLabel(components[i].toString()));

		for(int i = 0; i < components.length % (2*colpairs); ++i)
			newcomp[components.length + 2*colpairs + i] = Box.createGlue();

		return tabularAligned(BoxLayout.PAGE_AXIS, false, false, newcomp);
	}

	public static JPanel tabular(int majoraxis, boolean stretchMajor, boolean stretchMinor, GroupLayout.Alignment[] align, Component[][] components) {
		JPanel out = new JPanel();
		GroupLayout layout = new GroupLayout(out);
		out.setLayout(layout);

		layout.setAutoCreateGaps(true);
		layout.setAutoCreateContainerGaps(true);

		GroupLayout.Group majorGroup = layout.createSequentialGroup();
		GroupLayout.Group minorGroup = layout.createSequentialGroup();//layout.createParallelGroup(GroupLayout.Alignment.CENTER);
		GroupLayout.Group[] minors = new GroupLayout.Group[components[0].length];

		for(int y = 0; y < components.length; ++y) {
			GroupLayout.Group majorSubGroup = layout.createParallelGroup(majoraxis == BoxLayout.LINE_AXIS ? GroupLayout.Alignment.CENTER : GroupLayout.Alignment.BASELINE);

			for(int x = 0; x < components[y].length; ++x) {
				if(minors[x] == null)
					minors[x] = layout.createParallelGroup(align == null ? (majoraxis == BoxLayout.LINE_AXIS ? GroupLayout.Alignment.BASELINE : GroupLayout.Alignment.CENTER) : align[x]);

				if(stretchMajor)
					majorSubGroup.addComponent(components[y][x], GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE);
				else
					majorSubGroup.addComponent(components[y][x]);

				if(stretchMinor)
					minors[x].addComponent(components[y][x], GroupLayout.DEFAULT_SIZE, GroupLayout.DEFAULT_SIZE, Short.MAX_VALUE);
				else
					minors[x].addComponent(components[y][x]);
			}

			majorGroup.addGroup(majorSubGroup);
		}

		for(int x = 0; x < components[0].length; ++x)
			minorGroup.addGroup(minors[x]);

		if(majoraxis == BoxLayout.PAGE_AXIS) {
			layout.setVerticalGroup(majorGroup);
			layout.setHorizontalGroup(minorGroup);
		} else {
			layout.setVerticalGroup(minorGroup);
			layout.setHorizontalGroup(majorGroup);
		}

		return out;
	}

	public static JPanel tabular(int majorAxis, int majors, boolean stretchMajor, boolean stretchMinor, Object... components) {
		if(components.length % majors != 0)
			throw new IllegalArgumentException("Nonrectangular table.");

		Component[][] table = new Component[majors][components.length / majors];

		for(int y = 0; y < majors; ++y) {
			for(int x = 0; x < components.length / majors; ++x) {
				Object entry = components[y + x*majors];
				table[y][x] = (entry == null) ? Box.createGlue() : ((entry instanceof Component) ? (Component)entry : new JLabel(entry.toString()));
			}
		}

		return tabular(majorAxis, stretchMajor, stretchMinor, (GroupLayout.Alignment[]) null, table);
	}

	public static JPanel tabularAligned(int axis, boolean stretchMajor, boolean stretchMinor, Object... details) {
		int minors = 0;
		while(minors < details.length && details[minors] instanceof GroupLayout.Alignment) ++minors;

		if(details.length % minors != 0)
			throw new IllegalArgumentException("Nonrectangular table.");

		Component[][] table = new Component[details.length / minors - 1][minors];
		GroupLayout.Alignment[] aligns = Arrays.copyOfRange(details, 0, minors, GroupLayout.Alignment[].class);

		for(int y = 0; y < details.length / minors - 1; ++y) {
			for(int x = 0; x < minors; ++x) {
				Object entry = details[(y+1)*minors + x];
				table[y][x] = (entry == null) ? Box.createGlue() : ((entry instanceof Component) ? (Component)entry : new JLabel(entry.toString()));
			}
		}

		return tabular(axis, stretchMajor, stretchMinor, aligns, table);
	}
}

package org.omnetpp.scave.charting.plotter;

import org.eclipse.swt.graphics.GC;
import org.jfree.data.xy.XYDataset;
import org.omnetpp.scave.charting.ICoordsMapping;

public class DotsVectorPlotter extends VectorPlotter {

	public void plot(XYDataset dataset, int series, GC gc, ICoordsMapping mapping, IChartSymbol symbol) {
		plotSymbols(dataset, series, gc, mapping, symbol);
	}
}

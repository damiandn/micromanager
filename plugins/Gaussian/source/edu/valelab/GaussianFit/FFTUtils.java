/**
 * Utility function used in calculation of Power Spectra
 */
package edu.valelab.GaussianFit;

import edu.valelab.GaussianFit.utils.RowData;

import org.apache.commons.math3.complex.Complex;
import org.apache.commons.math3.transform.DftNormalization;
import org.apache.commons.math3.transform.FastFourierTransformer;
import org.apache.commons.math3.transform.TransformType;
import org.jfree.data.xy.XYSeries;

/**
 *
 * @author nico
 */
public class FFTUtils {
  /**
    * Calculates Power Spectrum density for the given datasets
    * and add result to a XYSeries for graphing using JFreeChart
    * Currently, the dataset is truncated to the highest power of two
    * Need to add pWelch windowing and zero-padding to next highest power of two
    * @param rowDatas
    * @param datas 
    */
   public static void calculatePSDs(RowData[] rowDatas, 
           XYSeries[] datas,
           DataCollectionForm.PlotMode plotMode) {
      for (int index = 0; index < rowDatas.length; index++) {
         FastFourierTransformer fft = new FastFourierTransformer(DftNormalization.STANDARD);
         datas[index] = new XYSeries(rowDatas[index].ID_);
         int length = rowDatas[index].spotList_.size();
         if (Integer.bitCount(length) != 1) { // N.B. equivalent to (formerly) FastFourierTransformer.isPowerOf2.
            length = Integer.highestOneBit(length) * 2; // N.B. equivalent to (formerly) FFTUtils.nextPowerOf2.
         }
         double[] d = new double[length];

         for (int i = 0; i < length; i++) {
            GaussianSpotData spot = rowDatas[index].spotList_.get(i);
            if (plotMode == DataCollectionForm.PlotMode.X)
               d[i] = spot.getXCenter();
            else if (plotMode == DataCollectionForm.PlotMode.Y)
               d[i] = spot.getYCenter();
            else if (plotMode == DataCollectionForm.PlotMode.INT)
               d[i] = spot.getIntensity();
         }
         Complex[] c = fft.transform(d, TransformType.FORWARD);
         int size = c.length / 2;
         double[] e = new double[size];
         double[] f = new double[size];
         double duration = rowDatas[index].timePoints_.get(length)
                 - rowDatas[index].timePoints_.get(0);
         double frequencyStep = 1000.0 / duration;
         // calculate the conjugate and normalize
         for (int i = 1; i < size; i++) {
            e[i] = (c[i].getReal() * c[i].getReal()
                    + c[i].getImaginary() * c[i].getImaginary()) / c.length;
            f[i] = frequencyStep * i;
            datas[index].add(f[i], e[i]);
         }
      }
   }
   
}

/**
 * Gaussian Fitting package
 * Implements MultiVariaRealFunction using the Gaussian functions defined in
 * GaussianUtils.  Implement MLE
 *
 * @author - Arthur Edelstein, September 2013
 */

package edu.valelab.GaussianFit;


import org.apache.commons.math3.analysis.MultivariateFunction;
import org.apache.commons.math3.analysis.MultivariateVectorFunction;
import org.apache.commons.math3.analysis.differentiation.DerivativeStructure;
import org.apache.commons.math3.analysis.differentiation.MultivariateDifferentiableFunction;

/**
 *
 * @author nico
 */
public class MultiVariateGaussianMLE implements MultivariateDifferentiableFunction {

   int[] data_;
   int nx_;
   int ny_;
   int count_ = 0;
   int mode_ = 1;



   /**
    * Gaussian fit can be run by estimating parameter c (width of Gaussian)
    * as 1 (circle), 2 (width varies in x and y), or 3 (ellipse) parameters
    *
    * @param dim
    */
   public MultiVariateGaussianMLE(int mode) {
      super();
      mode_ = mode;
   }

   public void setImage(short[] data, int width, int height) {
      data_ = new int[data.length];
      for (int i=0; i < data.length; i++) {
         data_[i] = (int) data [i] & 0xffff;
      }
      nx_ = width;
      ny_ = height;
   }

   // I expect it would be slower to call the method below from here, so I've left the original implementation intact.
   public double value(double[] params) {
       double residual = 0.0;
       if (mode_ == 1) {
          for (int i = 0; i < nx_; i++) {
             for (int j = 0; j < ny_; j++) {
                double expectation = GaussianUtils.gaussian(params, i, j);
                residual += expectation - data_[(j*nx_) + i] * Math.log(expectation);
             }
          }
       } else if (mode_ == 2) {
          for (int i = 0; i < nx_; i++) {
             for (int j = 0; j < ny_; j++) {
                double expectation = GaussianUtils.gaussian2DXY(params, i, j);
                residual += expectation - data_[(j*nx_) + i] * Math.log(expectation);
             }
          }
       } else if (mode_ == 3) {
          for (int i = 0; i < nx_; i++) {
             for (int j = 0; j < ny_; j++) {
                double expectation = GaussianUtils.gaussian2DEllips(params, i, j);
                residual += expectation - data_[(j*nx_) + i] * Math.log(expectation);
             }
          }
       }
       return residual;
   }

   public DerivativeStructure value(DerivativeStructure[] params) {
	   double[] dparams = new double[params.length];
	   for(int i=0; i < params.length; ++i)
		   dparams[i] = params[i].getValue();

       double[] mleGradient = new double[params.length + 1];
       mleGradient[0] = value(dparams);
       for (int i = 0; i < nx_; i++) {
          for (int j = 0; j < ny_; j++) {
             if (mode_ == 1) {
                double[] jacobian = GaussianUtils.gaussianJ(dparams, i, j);
                for (int k = 0; k < mleGradient.length; k++) {
                   mleGradient[k + 1] += jacobian[k] * (1 - data_[(j * nx_) + i] / GaussianUtils.gaussian(dparams, i, j));
                }
             }
             if (mode_ == 2) {
                double[] jacobian = GaussianUtils.gaussianJ2DXY(dparams, i, j);
                for (int k = 0; k < mleGradient.length; k++) {
                   mleGradient[k + 1] += jacobian[k] * (1 - data_[(j * nx_) + i] / GaussianUtils.gaussian2DXY(dparams, i, j));
                }
             }
             if (mode_ == 3) {
                double[] jacobian = GaussianUtils.gaussianJ2DEllips(dparams, i, j);
                for (int k = 0; k < mleGradient.length; k++) {
                   mleGradient[k + 1] += jacobian[k] * (1 - data_[(j * nx_) + i] / GaussianUtils.gaussian2DEllips(dparams, i, j));
                }
             }
          }
       }

	   return new DerivativeStructure(params.length, 1, mleGradient);
   }

/*   public MultivariateFunction partialDerivative(int i) {
	   
      throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
   }

   public MultivariateVectorFunction gradient() {
      
      MultivariateVectorFunction mVF = new MultivariateVectorFunction() {
         
         public double[] value(double[] params) throws IllegalArgumentException {
            double[] mleGradient = new double[params.length];
            for (int i = 0; i < nx_; i++) {
               for (int j = 0; j < ny_; j++) {
                  if (mode_ == 1) {
                     double[] jacobian = GaussianUtils.gaussianJ(params, i, j);
                     for (int k = 0; k < mleGradient.length; k++) {
                        mleGradient[k] += jacobian[k] * (1 - data_[(j * nx_) + i] / GaussianUtils.gaussian(params, i, j));
                     }
                  }
                  if (mode_ == 2) {
                     double[] jacobian = GaussianUtils.gaussianJ2DXY(params, i, j);
                     for (int k = 0; k < mleGradient.length; k++) {
                        mleGradient[k] += jacobian[k] * (1 - data_[(j * nx_) + i] / GaussianUtils.gaussian2DXY(params, i, j));
                     }
                  }
                  if (mode_ == 3) {
                     double[] jacobian = GaussianUtils.gaussianJ2DEllips(params, i, j);
                     for (int k = 0; k < mleGradient.length; k++) {
                        mleGradient[k] += jacobian[k] * (1 - data_[(j * nx_) + i] / GaussianUtils.gaussian2DEllips(params, i, j));
                     }
                  }
               }
            }
            return mleGradient;
         }
      };
      
      return mVF;
   }*/
   


}

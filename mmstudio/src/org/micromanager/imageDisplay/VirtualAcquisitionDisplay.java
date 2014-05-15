///////////////////////////////////////////////////////////////////////////////
//FILE:          VirtualAcquisitionDisplay.java
//PROJECT:       Micro-Manager
//SUBSYSTEM:     mmstudio
//-----------------------------------------------------------------------------
//
// AUTHOR:       Henry Pinkard, henry.pinkard@gmail.com
//               Arthur Edelstein, arthuredelstein@gmail.com
//
// COPYRIGHT:    University of California, San Francisco, 2013
//
// LICENSE:      This file is distributed under the BSD license.
//               License text is included with the source distribution.
//
//               This file is distributed in the hope that it will be useful,
//               but WITHOUT ANY WARRANTY; without even the implied warranty
//               of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
//               IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//               CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//               INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES.
//
package org.micromanager.imageDisplay;

import com.google.common.eventbus.EventBus;
import com.google.common.eventbus.Subscribe;

import ij.CompositeImage;
import ij.ImagePlus;
import ij.ImageStack;
import ij.WindowManager;
import ij.gui.ImageCanvas;
import ij.gui.ImageWindow;
import ij.gui.ScrollbarWithLabel;
import ij.gui.StackWindow;
import ij.gui.Toolbar;
import ij.io.FileInfo;
import ij.measure.Calibration;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.MouseInfo;
import java.awt.Point;
import java.awt.Toolkit;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.AdjustmentEvent;
import java.awt.event.AdjustmentListener;
import java.awt.event.MouseEvent;
import java.io.File;
import java.io.IOException;
import java.lang.Math;
import java.lang.reflect.InvocationTargetException;
import java.util.HashMap;
import java.util.TimerTask;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.prefs.Preferences;

import javax.swing.event.MouseInputAdapter;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JPopupMenu;
import javax.swing.SwingUtilities;
import javax.swing.Timer;

import mmcorej.TaggedImage;

import org.json.JSONException;
import org.json.JSONObject;
import org.micromanager.MMStudioMainFrame;
import org.micromanager.acquisition.AcquisitionEngine;
import org.micromanager.acquisition.TaggedImageStorageDiskDefault;
import org.micromanager.acquisition.TaggedImageStorageMultipageTiff;
import org.micromanager.api.events.PixelSizeChangedEvent;
import org.micromanager.api.ImageCache;
import org.micromanager.api.ImageCacheListener;
import org.micromanager.api.ScriptInterface;
import org.micromanager.api.TaggedImageStorage;
import org.micromanager.events.EventManager;
import org.micromanager.graph.HistogramControlsState;
import org.micromanager.graph.HistogramSettings;
import org.micromanager.graph.MultiChannelHistograms;
import org.micromanager.graph.SingleChannelHistogram;
import org.micromanager.internalinterfaces.DisplayControls;
import org.micromanager.internalinterfaces.Histograms;
import org.micromanager.utils.CanvasPaintPending;
import org.micromanager.utils.ContrastSettings;
import org.micromanager.utils.FileDialogs;
import org.micromanager.utils.GUIUtils;
import org.micromanager.utils.JavaUtils;
import org.micromanager.utils.MDUtils;
import org.micromanager.utils.MMScriptException;
import org.micromanager.utils.ReportingUtils;

public class VirtualAcquisitionDisplay implements ImageCacheListener {

   /**
    * Given an ImagePlus, retrieve the associated VirtualAcquisitionDisplay.
    * This only works if the ImagePlus is actually an AcquisitionVirtualStack;
    * otherwise you just get null.
    */
   public static VirtualAcquisitionDisplay getDisplay(ImagePlus imgp) {
      ImageStack stack = imgp.getStack();
      if (stack instanceof AcquisitionVirtualStack) {
         return ((AcquisitionVirtualStack) stack).getVirtualAcquisitionDisplay();
      } else {
         return null;
      }
   }

   /**
    * This class is used to signal when the animation state of our display
    * (potentially) changes.
    */
   public class AnimationSetEvent {
      public boolean isAnimated_;
      public AnimationSetEvent(boolean isAnimated) {
         isAnimated_ = isAnimated;
      }
   }

   private static final int ANIMATION_AND_LOCK_RESTART_DELAY = 800;
   final ImageCache imageCache_;
   final Preferences prefs_ = Preferences.userNodeForPackage(this.getClass());
   private static final String SIMPLE_WIN_X = "simple_x";
   private static final String SIMPLE_WIN_Y = "simple_y";
   private AcquisitionEngine eng_;
   private boolean finished_ = false;
   private boolean promptToSave_ = true;
   private String name_;
   private long lastDisplayTime_;
   private int lastFrameShown_ = 0;
   private int lastSliceShown_ = 0;
   private int lastPositionShown_ = 0;
   private int lockedSlice_ = -1, lockedPosition_ = -1, lockedChannel_ = -1, lockedFrame_ = -1;;
   private int numComponents_;
   private ImagePlus hyperImage_;
   private DisplayControls controls_;
   public AcquisitionVirtualStack virtualStack_;
   private boolean mda_ = false; //flag if display corresponds to MD acquisition
   private MetadataPanel mdPanel_;
   private boolean contrastInitialized_ = false; //used for autostretching on window opening
   private boolean firstImage_ = true;
   private String channelGroup_ = "none";
   private double framesPerSec_ = 7;
   private java.util.Timer animationTimer_ = new java.util.Timer();
   private boolean zAnimated_ = false, tAnimated_ = false;
   private int animatedSliceIndex_ = -1, animatedFrameIndex_ = -1;
   private Component zAnimationIcon_, pIcon_, tAnimationIcon_, cIcon_;
   private Component zLockIcon_, cLockIcon_, pLockIcon_, tLockIcon_;
   private Timer resetToLockedTimer_;
   private Histograms histograms_;
   private HistogramControlsState histogramControlsState_;
   private boolean albumSaved_ = false;
   private static double snapWinMag_ = -1;
   private JPopupMenu saveTypePopup_;
   private AtomicBoolean updatePixelSize_ = new AtomicBoolean(false);
   private AtomicLong newPixelSize_ = new AtomicLong();
   private final Object imageReceivedObject_ = new Object();

   private EventBus bus_;

   @Subscribe
   public void onPixelSizeChanged(PixelSizeChangedEvent event) {
      // Signal that pixel size has changed so that the next image will update
      // metadata and scale bar
      newPixelSize_.set(Double.doubleToLongBits(event.getNewPixelSizeUm()));
      updatePixelSize_.set(true);
   }

   /**
    * Constructor that doesn't provide a title; an automatically-generated one
    * is used instead.
    */
   public VirtualAcquisitionDisplay(ImageCache imageCache, AcquisitionEngine eng) {
      this(imageCache, eng, WindowManager.getUniqueName("Untitled"));
      setupEventBus();
   }

   /**
    * Standard constructor.
    */
   public VirtualAcquisitionDisplay(ImageCache imageCache, AcquisitionEngine eng, String name) {
      name_ = name;
      imageCache_ = imageCache;
      eng_ = eng;
      mda_ = eng != null;
      this.albumSaved_ = imageCache.isFinished();
      setupEventBus();
   }

   /**
    * Create a new EventBus that will be used for all events related to this
    * display system.
    */
   private void setupEventBus() {
      bus_ = new EventBus();
      bus_.register(this);
   }

   // Retrieve our EventBus.
   public EventBus getEventBus() {
      return bus_;
   }

   // Prepare for a drawing event.
   @Subscribe
   public void onDraw(DrawEvent event) {
      imageChangedUpdate();
   }

   /**
    * This constructor is used for the Snap and Live views. The main
    * differences:
    * - eng_ is null
    * - We subscribe to the "pixel size changed" event. 
    */
   @SuppressWarnings("LeakingThisInConstructor")
   public VirtualAcquisitionDisplay(ImageCache imageCache, String name) throws MMScriptException {
      imageCache_ = imageCache;
      name_ = name;
      mda_ = false;
      this.albumSaved_ = imageCache.isFinished();
      setupEventBus();
      // Also register us for pixel size change events on the global EventBus.
      EventManager.register(this);
   }
  
   /**
    * Extract a lot of fields from the provided metadata (or, failing that, 
    * from getSummaryMetadata()), and set up our controls and view window.
    */
   private void startup(JSONObject firstImageMetadata, AcquisitionVirtualStack virtualStack) {
      mdPanel_ = MMStudioMainFrame.getInstance().getMetadataPanel();
      JSONObject summaryMetadata = getSummaryMetadata();
      int numSlices = 1;
      int numFrames = 1;
      int numChannels = 1;
      int numGrayChannels;
      int numPositions = 1;
      int width = 0;
      int height = 0;
      int numComponents = 1;
      try {
         int imageChannelIndex;
         if (firstImageMetadata != null) {
            width = MDUtils.getWidth(firstImageMetadata);
            height = MDUtils.getHeight(firstImageMetadata);
            try {
               imageChannelIndex = MDUtils.getChannelIndex(firstImageMetadata);
            } catch (JSONException e) {
               imageChannelIndex = -1;
            }
         } else {
            width = MDUtils.getWidth(summaryMetadata);
            height = MDUtils.getHeight(summaryMetadata);
            imageChannelIndex = -1;
         }
         numSlices = Math.max(summaryMetadata.getInt("Slices"), 1);
         numFrames = Math.max(summaryMetadata.getInt("Frames"), 1);

         numChannels = Math.max(1 + imageChannelIndex,
                 Math.max(summaryMetadata.getInt("Channels"), 1));
         numPositions = Math.max(summaryMetadata.getInt("Positions"), 1);
         numComponents = Math.max(MDUtils.getNumberOfComponents(summaryMetadata), 1);
      } catch (JSONException e) {
         ReportingUtils.showError(e);
      } catch (MMScriptException e) {
         ReportingUtils.showError(e);
      }
      numComponents_ = numComponents;
      numGrayChannels = numComponents_ * numChannels;

      if (imageCache_.getDisplayAndComments() == null || 
            imageCache_.getDisplayAndComments().isNull("Channels")) {
         try {
            imageCache_.setDisplayAndComments(DisplaySettings.getDisplaySettingsFromSummary(summaryMetadata));
         } catch (Exception ex) {
            ReportingUtils.logError(ex, "Problem setting display and Comments");
         }
      }

      int type = 0;
      try {
         if (firstImageMetadata != null) {
            type = MDUtils.getSingleChannelType(firstImageMetadata);
         } else {
            type = MDUtils.getSingleChannelType(summaryMetadata);
         }
      } catch (JSONException ex) {
         ReportingUtils.showError(ex, "Unable to determine acquisition type.");
      } catch (MMScriptException ex) {
         ReportingUtils.showError(ex, "Unable to determine acquisition type.");
      }
      if (virtualStack != null) {
         virtualStack_ = virtualStack;
      } else {
         virtualStack_ = new AcquisitionVirtualStack(width, height, type, null,
                 imageCache_, numGrayChannels * numSlices * numFrames, this);
      }
      if (summaryMetadata.has("PositionIndex")) {
         try {
            virtualStack_.setPositionIndex(
                  MDUtils.getPositionIndex(summaryMetadata));
         } catch (JSONException ex) {
            ReportingUtils.logError(ex);
         }
      }
      // Hack: allow controls_ to be already set, so that overriding classes
      // can implement their own custom controls.
      if (controls_ == null) {
         controls_ = new HyperstackControls(this, bus_);
      }
      hyperImage_ = createHyperImage(createMMImagePlus(virtualStack_),
              numGrayChannels, numSlices, numFrames, virtualStack_);

      applyPixelSizeCalibration(hyperImage_);

      histogramControlsState_ =  mdPanel_.getContrastPanel().createDefaultControlsState();
      createWindow();
      windowToFront();

      updateAndDraw(true);
      updateWindowTitleAndStatus();
   }

   /*
    * Set display to one of three modes:
    * ij.CompositeImage.COMPOSITE
    * ij.CompositeImage.GRAYSCALE
    * ij.CompositeImage.COLOR
    */
   public void setDisplayMode(int displayMode) {
      mdPanel_.getContrastPanel().setDisplayMode(displayMode);
   }
   
   /**
    * Repaint all of our icons related to the scrollbars. Non-blocking.
    */
   private void refreshScrollbarIcons() {
      if (zAnimationIcon_ != null) {
         zAnimationIcon_.repaint();
      }
      if (tAnimationIcon_ != null) {
         tAnimationIcon_.repaint();
      }
      if (zLockIcon_ != null) {
         zLockIcon_.repaint();
      }
      if (cLockIcon_ != null) {
         cLockIcon_.repaint();
      }
      if (pLockIcon_ != null) {
         pLockIcon_.repaint();
      }
      if (tLockIcon_ != null) {
         tLockIcon_.repaint();
      }
   }

   /**
    * Allows bypassing the prompt to Save
    * @param promptToSave boolean flag
    */
   public void promptToSave(boolean promptToSave) {
      promptToSave_ = promptToSave;
   }

   /**
    * required by ImageCacheListener
    * @param taggedImage 
    */
   @Override
   public  void imageReceived(final TaggedImage taggedImage) {
      if (hyperImage_ == null) {
         updateDisplay(taggedImage, false);
         return;
      }
      if (!CanvasPaintPending.isMyPaintPending(hyperImage_.getCanvas(), imageReceivedObject_)) {
         // If we do not sleep here, the window never updates
         // I do not understand why, but this fixes it
         try {
            Thread.sleep(25);
         } catch (InterruptedException ex) {
            ReportingUtils.logError(ex, "Sleeping Thread was woken");
         }
         CanvasPaintPending.setPaintPending(hyperImage_.getCanvas(), imageReceivedObject_);
         updateDisplay(taggedImage, false);
         
      }
   }

   /**
    * Method required by ImageCacheListener
    * @param path
    */
   @Override
   public void imagingFinished(String path) {
      updateDisplay(null, true);
      updateAndDraw(true);
      if (!(eng_ != null && eng_.abortRequested())) {
         updateWindowTitleAndStatus();
      }
   }

   private void updateDisplay(TaggedImage taggedImage, boolean finalUpdate) {
      try {
         long t = System.currentTimeMillis();
         JSONObject tags;
         if (taggedImage != null) {
            tags = taggedImage.tags;
         } else {
            tags = imageCache_.getLastImageTags();
         }
         if (tags == null) {
            return;
         }
         int frame = MDUtils.getFrameIndex(tags);
         int ch = MDUtils.getChannelIndex(tags);
         int slice = MDUtils.getSliceIndex(tags);
         int position = MDUtils.getPositionIndex(tags);

         int updateTime = 30;
         //update display if: final update, frame is 0, more than 30 ms since last update, 
         //last channel for given frame/slice/position, or final slice and channel for first frame and position
         boolean show = finalUpdate || frame == 0 || (Math.abs(t - lastDisplayTime_) > updateTime)
                 || (ch == getNumChannels() - 1 && lastFrameShown_ == frame && lastSliceShown_ == slice && lastPositionShown_ == position)
                 || (slice == getNumSlices() - 1 && frame == 0 && position == 0 && ch == getNumChannels() - 1);

         if (show) {
            showImage(tags, true);
            lastFrameShown_ = frame;
            lastSliceShown_ = slice;
            lastPositionShown_ = position;
            lastDisplayTime_ = t;
         }  
      } catch (JSONException e) {
         ReportingUtils.logError(e);
      } catch (InterruptedException e) {
         ReportingUtils.logError(e);
      } catch (InvocationTargetException e) {
         ReportingUtils.logError(e);
      }
   }

   public int rgbToGrayChannel(int channelIndex) {
      try {
         if (MDUtils.getNumberOfComponents(imageCache_.getSummaryMetadata()) == 3) {
            return channelIndex * 3;
         }
         return channelIndex;
      } catch (MMScriptException ex) {
         ReportingUtils.logError(ex);
         return 0;
      } catch (JSONException ex) {
         ReportingUtils.logError(ex);
         return 0;
      }
   }

   public int grayToRGBChannel(int grayIndex) {
      try {
         if (imageCache_ != null) {
            if (imageCache_.getSummaryMetadata() != null)
            if (MDUtils.getNumberOfComponents(imageCache_.getSummaryMetadata()) == 3) {
               return grayIndex / 3;
            }
         }
         return grayIndex;
      } catch (MMScriptException ex) {
         ReportingUtils.logError(ex);
         return 0;
      } catch (JSONException ex) {
         ReportingUtils.logError(ex);
         return 0;
      }
   }

   /**
    * Sets ImageJ pixel size calibration
    * @param hyperImage
    */
   private void applyPixelSizeCalibration(final ImagePlus hyperImage) {
      final String pixSizeTag = "PixelSizeUm";
      try {
         JSONObject tags = this.getCurrentMetadata();
         JSONObject summary = getSummaryMetadata();
         double pixSizeUm;
         if (tags != null && tags.has(pixSizeTag)) {
            pixSizeUm = tags.getDouble(pixSizeTag);
         } else {
            pixSizeUm = summary.getDouble("PixelSize_um");
         }
         if (pixSizeUm > 0) {
            Calibration cal = new Calibration();
            cal.setUnit("um");
            cal.pixelWidth = pixSizeUm;
            cal.pixelHeight = pixSizeUm;
            String intMs = "Interval_ms";
            if (summary.has(intMs))
               cal.frameInterval = summary.getDouble(intMs) / 1000.0;
            String zStepUm = "z-step_um";
            if (summary.has(zStepUm))
               cal.pixelDepth = summary.getDouble(zStepUm);
            hyperImage.setCalibration(cal);
            // this call is needed to update the top status line with image size
            ImageWindow win = hyperImage.getWindow();
            if (win != null) {
               win.repaint();
            }
         }
      } catch (JSONException ex) {
         // no pixelsize defined.  Nothing to do
      }
   }

   public ImagePlus getHyperImage() {
      return hyperImage_;
   }

   public int getStackSize() {
      if (hyperImage_ == null) {
         return -1;
      }
      int s = hyperImage_.getNSlices();
      int c = hyperImage_.getNChannels();
      int f = hyperImage_.getNFrames();
      if ((s > 1 && c > 1) || (c > 1 && f > 1) || (f > 1 && s > 1)) {
         return s * c * f;
      }
      return Math.max(Math.max(s, c), f);
   }

   private void imageChangedWindowUpdate() {
      if (hyperImage_ != null && hyperImage_.isVisible()) {
         JSONObject md = getCurrentMetadata();
         if (md != null) {
            controls_.newImageUpdate(md);
         }
      }
   }
   
   public void updateAndDraw(boolean force) {
      imageChangedUpdate();
      if (hyperImage_ != null && hyperImage_.isVisible()) {  
         if (hyperImage_ instanceof MMCompositeImage) {                   
            ((MMCompositeImage) hyperImage_).updateAndDraw(force);
         } else {
            hyperImage_.updateAndDraw();
         }
      }
   }

   public void updateWindowTitleAndStatus() {
      if (controls_ == null) {
         return;
      }

      String status = "";
      final AcquisitionEngine eng = eng_;

      if (eng != null) {
         if (acquisitionIsRunning()) {
            if (!abortRequested()) {
               controls_.acquiringImagesUpdate(true);
               if (isPaused()) {
                  status = "paused";
               } else {
                  status = "running";
               }
            } else {
               controls_.acquiringImagesUpdate(false);
               status = "interrupted";
            }
         } else {
            controls_.acquiringImagesUpdate(false);
            if (!status.contentEquals("interrupted")) {
               if (eng.isFinished()) {
                  status = "finished";
                  eng_ = null;
               }
            }
         }
         status += ", ";
         if (eng.isFinished()) {
            eng_ = null;
            finished_ = true;
         }
      } else {
         if (finished_ == true) {
            status = "finished, ";
         }
         controls_.acquiringImagesUpdate(false);
      }
      if (isDiskCached() || albumSaved_) {
         status += "on disk";
      } else {
         status += "not yet saved";
      }

      controls_.imagesOnDiskUpdate(imageCache_.getDiskLocation() != null);
      String path = isDiskCached()
              ? new File(imageCache_.getDiskLocation()).getName() : name_;

      if (hyperImage_.isVisible()) {
         int mag = (int) (100 * hyperImage_.getCanvas().getMagnification());
         hyperImage_.getWindow().setTitle(path + " (" + status + ") (" + mag + "%)" );
      }

   }

   private void windowToFront() {
      if (hyperImage_ == null || hyperImage_.getWindow() == null) {
         return;
      }
      hyperImage_.getWindow().toFront();
   }

   /**
    * Displays tagged image in the multi-D viewer
    * Will wait for the screen update
    *      
    * @param taggedImg
    * @throws Exception 
    */
   public void showImage(TaggedImage taggedImg) throws Exception {
      showImage(taggedImg, true);
   }

   /**
    * Displays tagged image in the multi-D viewer
    * Optionally waits for the display to draw the image
    *     * 
    * @param taggedImg 
    * @param waitForDisplay 
    * @throws java.lang.InterruptedException 
    * @throws java.lang.reflect.InvocationTargetException 
    */
   public void showImage(TaggedImage taggedImg, boolean waitForDisplay) throws InterruptedException, InvocationTargetException {
      showImage(taggedImg.tags, waitForDisplay);
   }
   
   public void showImage(final JSONObject tags, boolean waitForDisplay) throws InterruptedException, InvocationTargetException {
      SwingUtilities.invokeLater( new Runnable() {
         @Override
         public void run() {

            updateWindowTitleAndStatus();

            if (tags == null) {
               return;
            }

            if (hyperImage_ == null) {
               // this has to run on the EDT
               startup(tags, null);
            }

            int channel = 0, frame = 0, slice = 0, position = 0, superChannel = 0;
            try {
               frame = MDUtils.getFrameIndex(tags);
               slice = MDUtils.getSliceIndex(tags);
               channel = MDUtils.getChannelIndex(tags);
               position = MDUtils.getPositionIndex(tags);
               superChannel = VirtualAcquisitionDisplay.this.rgbToGrayChannel(
                       MDUtils.getChannelIndex(tags));
               // Construct a mapping of axis to position so we can post an 
               // event informing others of the new image.
               HashMap<String, Integer> axisToPosition = new HashMap<String, Integer>();
               axisToPosition.put("channel", channel);
               axisToPosition.put("position", position);
               axisToPosition.put("time", frame);
               axisToPosition.put("z", slice);
               bus_.post(new NewImageEvent(axisToPosition));
            } catch (JSONException ex) {
               ReportingUtils.logError(ex);
            }

            //make sure pixels get properly set
            if (hyperImage_ != null && frame == 0) {
               IMMImagePlus img = (IMMImagePlus) hyperImage_;
               if (img.getNChannelsUnverified() == 1) {
                  if (img.getNSlicesUnverified() == 1) {
                     hyperImage_.getProcessor().setPixels(virtualStack_.getPixels(1));
                  }
               } else if (hyperImage_ instanceof MMCompositeImage) {
                  //reset rebuilds each of the channel ImageProcessors with the correct pixels
                  //from AcquisitionVirtualStack
                  MMCompositeImage ci = ((MMCompositeImage) hyperImage_);
                  ci.reset();
                  //This line is neccessary for image processor to have correct pixels in grayscale mode
                  ci.getProcessor().setPixels(virtualStack_.getPixels(ci.getCurrentSlice()));
               }
            } else if (hyperImage_ instanceof MMCompositeImage) {
               MMCompositeImage ci = ((MMCompositeImage) hyperImage_);
               ci.reset();
            }

            IMMImagePlus immi = (IMMImagePlus) hyperImage_;
            // Ensure proper dimensions are set on the image.
            if (immi.getNFramesUnverified() <= frame) {
               immi.setNFramesUnverified(frame);
            }  
            if (immi.getNSlicesUnverified() <= slice) {
               immi.setNSlicesUnverified(slice);
            }  
            if (immi.getNChannelsUnverified() <= channel) {
               immi.setNChannelsUnverified(channel);
            }

            if (frame == 0) {
               initializeContrast();
            }

            updateAndDraw(true);

            //get channelgroup name for use in loading contrast setttings
            if (firstImage_) {
               try {
                  channelGroup_ = tags.getString("Core-ChannelGroup");
               } catch (JSONException ex) {
                  ReportingUtils.logError("Couldn't find Core-ChannelGroup in image metadata");
               }
               firstImage_ = false;
            }
         }
      });
   }

   private void initializeContrast() {
      if (contrastInitialized_ ) {
         return;
      }
      int numChannels = imageCache_.getNumDisplayChannels();
      
      for (int channel = 0; channel < numChannels; channel++) {
         String channelName = imageCache_.getChannelName(channel);
         HistogramSettings settings = MMStudioMainFrame.getInstance().loadStoredChannelHisotgramSettings(
                 channelGroup_, channelName, mda_);
         histograms_.setChannelContrast(channel, settings.min_, settings.max_, settings.gamma_);
         histograms_.setChannelHistogramDisplayMax(channel, settings.histMax_);
         if (imageCache_.getNumDisplayChannels() > 1) {
            setDisplayMode(settings.displayMode_);
         }
      }
      histograms_.applyLUTToImage();
      contrastInitialized_ = true;
   }

   public void storeChannelHistogramSettings(int channelIndex, int min, int max, 
           double gamma, int histMax, int displayMode) {
     if (!contrastInitialized_ ) {
        return; //don't erroneously initialize c   ontrast
     }
      //store for this dataset
      imageCache_.storeChannelDisplaySettings(channelIndex, min, max, gamma, histMax, displayMode);
      //store global preference for channel contrast settings
      if (mda_) {
         //only store for datasets that were just acquired or snap/live (i.e. no loaded datasets)
         MMStudioMainFrame.getInstance().saveChannelHistogramSettings(channelGroup_, 
                 imageCache_.getChannelName(channelIndex), mda_,
                 new HistogramSettings(min,max, gamma, histMax, displayMode));    
      }   
   }

   protected void updatePosition(int p) {
      virtualStack_.setPositionIndex(p);
      if (!hyperImage_.isComposite()) {
         Object pixels = virtualStack_.getPixels(hyperImage_.getCurrentSlice());
         hyperImage_.getProcessor().setPixels(pixels);
      } else {
         CompositeImage ci = (CompositeImage) hyperImage_;
         if (ci.getMode() == CompositeImage.COMPOSITE) {
            for (int i = 0; i < ((MMCompositeImage) ci).getNChannelsUnverified(); i++) {
               //Dont need to set pixels if processor is null because it will get them from stack automatically  
               if (ci.getProcessor(i + 1) != null)                
                  ci.getProcessor(i + 1).setPixels(virtualStack_.getPixels(ci.getCurrentSlice() - ci.getChannel() + i + 1));
            }
         }
         ci.getProcessor().setPixels(virtualStack_.getPixels(hyperImage_.getCurrentSlice()));
      }
      //need to call this even though updateAndDraw also calls it to get autostretch to work properly
      imageChangedUpdate();
      updateAndDraw(true);
   }

   public void setSliceIndex(int i) {
      final int f = hyperImage_.getFrame();
      final int c = hyperImage_.getChannel();
      hyperImage_.setPosition(c, i + 1, f);
   }

   public int getSliceIndex() {
      return hyperImage_.getSlice() - 1;
   }

   boolean pause() {
      if (eng_ != null) {
         if (eng_.isPaused()) {
            eng_.setPause(false);
         } else {
            eng_.setPause(true);
         }
         updateWindowTitleAndStatus();
         return (eng_.isPaused());
      }
      return false;
   }

   public boolean abort() {
      if (eng_ != null) {
         if (eng_.abortRequest()) {
            updateWindowTitleAndStatus();
            return true;
         }
      }
      return false;
   }

   public boolean acquisitionIsRunning() {
      if (eng_ != null) {
         return eng_.isAcquisitionRunning();
      } else {
         return false;
      }
   }

   public long getNextWakeTime() {
      return eng_.getNextWakeTime();
   }

   public boolean abortRequested() {
      if (eng_ != null) {
         return eng_.abortRequested();
      } else {
         return false;
      }
   }

   private boolean isPaused() {
      if (eng_ != null) {
         return eng_.isPaused();
      } else {
         return false;
      }
   }

   public void albumChanged() {
      albumSaved_ = false;
   }
   
   private Class createSaveTypePopup() {
      if (saveTypePopup_ != null) {
         saveTypePopup_.setVisible(false);
         saveTypePopup_ = null;
      }
      final JPopupMenu menu = new JPopupMenu();
      saveTypePopup_ = menu;
      JMenuItem single = new JMenuItem("Save as separate image files");
      JMenuItem multi = new JMenuItem("Save as image stack file");
      JMenuItem cancel = new JMenuItem("Cancel");
      menu.add(single);
      menu.add(multi);
      menu.addSeparator();
      menu.add(cancel);
      final AtomicInteger ai = new AtomicInteger(-1);
      cancel.addActionListener(new ActionListener() {
         @Override
         public void actionPerformed(ActionEvent e) {
            ai.set(0);
         }
      });
      single.addActionListener(new ActionListener() {
         @Override
         public void actionPerformed(ActionEvent e) {
            ai.set(1);
         }
      });
      multi.addActionListener(new ActionListener() {
         @Override
         public void actionPerformed(ActionEvent e) {
            ai.set(2);
         }
      });
      MouseInputAdapter highlighter = new MouseInputAdapter() {
         @Override
         public void mouseEntered(MouseEvent e) {
            ((JMenuItem) e.getComponent()).setArmed(true);
         }
         @Override
         public void mouseExited(MouseEvent e) {
            ((JMenuItem) e.getComponent()).setArmed(false);
         }       
      };
      single.addMouseListener(highlighter);
      multi.addMouseListener(highlighter);
      cancel.addMouseListener(highlighter);  
      Point mouseLocation = MouseInfo.getPointerInfo().getLocation();
      menu.show(null, mouseLocation.x, mouseLocation.y);
      while (ai.get() == -1) {
         try {
            Thread.sleep(10);
         } catch (InterruptedException ex) {}
         if (!menu.isVisible()) {
            return null;
         }
      }
      menu.setVisible(false);
      saveTypePopup_ = null;
      if (ai.get() == 0) {
         return null;
      } else if (ai.get() == 1) {
         return TaggedImageStorageDiskDefault.class;
      } else {
         return TaggedImageStorageMultipageTiff.class;
      }  
   }

   boolean saveAs() {
      return saveAs(null,true);
   }

   boolean saveAs(boolean pointToNewStorage) {
      return saveAs(null, pointToNewStorage);
   }

   private boolean saveAs(Class<?> storageClass, boolean pointToNewStorage) {
      if (eng_ != null && eng_.isAcquisitionRunning()) {
         JOptionPane.showMessageDialog(null, 
                 "Data can not be saved while acquisition is running.");
         return false;
      }
      if (storageClass == null) {
         storageClass = createSaveTypePopup();
      }
      if (storageClass == null) {
         return false;
      }
      String prefix;
      String root;
      for (;;) {
         File f = FileDialogs.save(hyperImage_.getWindow(),
                 "Please choose a location for the data set",
                 MMStudioMainFrame.MM_DATA_SET);
         if (f == null) // Canceled.
         {
            return false;
         }
         prefix = f.getName();
         root = new File(f.getParent()).getAbsolutePath();
         if (f.exists()) {
            ReportingUtils.showMessage(prefix
                    + " is write only! Please choose another name.");
         } else {
            break;
         }
      }

      try {
         if (getSummaryMetadata() != null) {
            getSummaryMetadata().put("Prefix", prefix);
         }
         TaggedImageStorage newFileManager =
                 (TaggedImageStorage) storageClass.getConstructor(
                 String.class, Boolean.class, JSONObject.class).newInstance(
                 root + "/" + prefix, true, getSummaryMetadata());
         if (pointToNewStorage) {
            albumSaved_ = true;
         }

         imageCache_.saveAs(newFileManager, pointToNewStorage);
      } catch (IllegalAccessException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      } catch (IllegalArgumentException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      } catch (InstantiationException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      } catch (NoSuchMethodException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      } catch (SecurityException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      } catch (InvocationTargetException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      } catch (JSONException ex) {
         ReportingUtils.showError(ex, "Failed to save file");
      }
      MMStudioMainFrame.getInstance().setAcqDirectory(root);
      updateWindowTitleAndStatus();
      return true;
   }

   final public MMImagePlus createMMImagePlus(AcquisitionVirtualStack virtualStack) {
      MMImagePlus img = new MMImagePlus(imageCache_.getDiskLocation(), 
            virtualStack, virtualStack.getVirtualAcquisitionDisplay().getEventBus());
      FileInfo fi = new FileInfo();
      fi.width = virtualStack.getWidth();
      fi.height = virtualStack.getHeight();
      fi.fileName = virtualStack.getDirectory();
      fi.url = null;
      img.setFileInfo(fi);
      return img;
   }

   final public ImagePlus createHyperImage(MMImagePlus mmIP, int channels, int slices,
           int frames, final AcquisitionVirtualStack virtualStack) {
      final ImagePlus hyperImage;
      mmIP.setNChannelsUnverified(channels);
      mmIP.setNFramesUnverified(frames);
      mmIP.setNSlicesUnverified(slices);
      if (channels > 1) {        
         hyperImage = new MMCompositeImage(mmIP, imageCache_.getDisplayMode(), 
               name_, bus_);
         hyperImage.setOpenAsHyperStack(true);
      } else {
         hyperImage = mmIP;
         mmIP.setOpenAsHyperStack(true);
      }
      return hyperImage;
   }

   private void createWindow() {
      makeHistograms();
      final DisplayWindow win = new DisplayWindow(hyperImage_, bus_);
      win.getCanvas().addMouseListener(new MouseInputAdapter() {
         //updates the histogram after an ROI is drawn
         @Override
         public void mouseReleased(MouseEvent me) {
            if (hyperImage_ instanceof MMCompositeImage) {
               ((MMCompositeImage) hyperImage_).updateAndDraw(true);
            } else {
               hyperImage_.updateAndDraw();
            }
         }
      });

      win.setBackground(MMStudioMainFrame.getInstance().getBackgroundColor());
      MMStudioMainFrame.getInstance().addMMBackgroundListener(win);

      win.add(controls_);
      win.pack();

      //Set magnification
      zoomToPreferredSize(win);
   
      mdPanel_.displayChanged(win);
      imageChangedUpdate();
   }
   
   private void zoomToPreferredSize(DisplayWindow win) {
      Point location = win.getLocation();
      win.setLocation(new Point(0,0));
      
      double mag = MMStudioMainFrame.getInstance().getPreferredWindowMag();

      ImageCanvas canvas = win.getCanvas();
      if (mag < canvas.getMagnification()) {
         while (mag < canvas.getMagnification()) {
            canvas.zoomOut(canvas.getWidth() / 2, canvas.getHeight() / 2);
         }
      } else if (mag > canvas.getMagnification()) {

         while (mag > canvas.getMagnification()) {
            canvas.zoomIn(canvas.getWidth() / 2, canvas.getHeight() / 2);
         }
      }

      //Make sure the window is fully on the screen
      Dimension screenSize = Toolkit.getDefaultToolkit().getScreenSize();
      Point newLocation = new Point(location.x,location.y);
      if (newLocation.x + win.getWidth() > screenSize.width && win.getWidth() < screenSize.width) {
          newLocation.x = screenSize.width - win.getWidth();
      }
      if (newLocation.y + win.getHeight() > screenSize.height && win.getHeight() < screenSize.height) {
          newLocation.y = screenSize.height - win.getHeight();
      }
      
      win.setLocation(newLocation);
   }

   // A window wants to close; check if it's okay. If it is, then we call its
   // forceClosed() function.
   // TODO: for now, assuming we only have one window.
   @Subscribe
   public void onWindowClose(DisplayWindow.WindowClosingEvent event) {
      if (eng_ != null && eng_.isAcquisitionRunning()) {
         if (!abort()) {
            // Can't close now; the acquisition is still running.
            return;
         }
      }
      // Ask if the user wants to save data.

      if (imageCache_.getDiskLocation() == null && 
            promptToSave_ && !albumSaved_) {
         String[] options = {"Save single", "Save multi", "No", "Cancel"};
         int result = JOptionPane.showOptionDialog(
               event.window_, "This data set has not yet been saved. " + 
               "Do you want to save it?\n" + 
               "Data can be saved as single-image files or multi-image files.",
               "Micro-Manager", 
               JOptionPane.DEFAULT_OPTION, JOptionPane.QUESTION_MESSAGE, null,
               options, options[0]);

         if (result == 0) {
            if (!saveAs(TaggedImageStorageDiskDefault.class, true)) {
               return;
            }
         } else if (result == 1) {
            if (!saveAs(TaggedImageStorageMultipageTiff.class, true)) {
               return;
            }
         } else if (result == 3) {
            return;
         }
      }

      if (imageCache_ != null) {
         imageCache_.close();
      }

      removeFromAcquisitionManager(MMStudioMainFrame.getInstance());

      //Call this because for some reason WindowManager doesnt always fire
      mdPanel_.displayChanged(null);
      animationTimer_.cancel();
      animationTimer_.cancel();

      // Finally, tell the window to close now.
      DisplayWindow window = event.window_;
      window.forceClosed();
   }

   /*
    * Removes the VirtualAcquisitionDisplay from the Acquisition Manager.
    */
   private void removeFromAcquisitionManager(ScriptInterface gui) {
      try {
         if (gui.acquisitionExists(name_)) {
            gui.closeAcquisition(name_);
         }
      } catch (MMScriptException ex) {
         ReportingUtils.logError(ex);
      }
   }

   //Return metadata associated with image currently shown in the viewer
   public JSONObject getCurrentMetadata() {
      if (hyperImage_ != null) {
         JSONObject md = virtualStack_.getImageTags(hyperImage_.getCurrentSlice());
         return md;
      } else {
         return null;
      }
   }

   public int getCurrentPosition() {
      return virtualStack_.getPositionIndex();
   }

   public int getNumSlices() {
      return hyperImage_ == null ? 1 : ((IMMImagePlus) hyperImage_).getNSlicesUnverified();
   }

   public int getNumFrames() {
      return ((IMMImagePlus) hyperImage_).getNFramesUnverified();
   }

   public int getNumPositions() {
      return 1;
   }

   public ImagePlus getImagePlus() {
      return hyperImage_;
   }

   public ImageCache getImageCache() {
      return imageCache_;
   }

   public ImagePlus getImagePlus(int position) {
      ImagePlus iP = new ImagePlus();
      iP.setStack(virtualStack_);
      iP.setDimensions(numComponents_ * getNumChannels(), getNumSlices(), getNumFrames());
      iP.setFileInfo(hyperImage_.getFileInfo());
      return iP;
   }

   public void setComment(String comment) throws MMScriptException {
      try {
         getSummaryMetadata().put("Comment", comment);
      } catch (JSONException ex) {
         ReportingUtils.logError(ex);
      }
   }

   public final JSONObject getSummaryMetadata() {
      return imageCache_.getSummaryMetadata();
   }
   
   /*
   public final JSONObject getImageMetadata(int channel, int slice, int frame, int position) {
      return imageCache_.getImageTags(channel, slice, frame, position);
   }
    */

   /**
    * Closes the ImageWindow and associated ImagePlus
    * 
    * @return false if canceled by user, true otherwise 
    */
   public boolean close() {
      try {
         if (hyperImage_ != null) {
            if (!hyperImage_.getWindow().close()) {
               return false;
            }
            hyperImage_.close();
         }
      } catch (NullPointerException npe) {
         // instead of handing when exiting MM, log the issue
         ReportingUtils.logError(npe);
      }
      return true;
   }

   public synchronized boolean windowClosed() {
      if (hyperImage_ != null) {
         ImageWindow win = hyperImage_.getWindow();
         return (win == null || win.isClosed());
      }
      return true;
   }

   public void showFolder() {
      if (isDiskCached()) {
         try {
            File location = new File(imageCache_.getDiskLocation());
            if (JavaUtils.isWindows()) {
               Runtime.getRuntime().exec("Explorer /n,/select," + location.getAbsolutePath());
            } else if (JavaUtils.isMac()) {
               if (!location.isDirectory()) {
                  location = location.getParentFile();
               }
               Runtime.getRuntime().exec("open " + location.getAbsolutePath());
            }
         } catch (IOException ex) {
            ReportingUtils.logError(ex);
         }
      }
   }

   public String getSummaryComment() {
      return imageCache_.getComment();
   }

   public void setSummaryComment(String comment) {
      imageCache_.setComment(comment);
   }

   void setImageComment(String comment) {
      imageCache_.setImageComment(comment, getCurrentMetadata());
   }

   String getImageComment() {
      try {
         return imageCache_.getImageComment(getCurrentMetadata());
      } catch (NullPointerException ex) {
         return "";
      }
   }

   public boolean isDiskCached() {
      ImageCache imageCache = imageCache_;
      if (imageCache == null) {
         return false;
      } else {
         return imageCache.getDiskLocation() != null;
      }
   }

   //This method exists in addition to the other show method
   // so that plugins can utilize virtual acqusition display with a custom virtual stack
   //allowing manipulation of displayed images without changing underlying data
   //should probably be reconfigured to work through some sort of interface in the future
   public void show(final AcquisitionVirtualStack virtualStack) {
      if (hyperImage_ == null) {
         try {
            GUIUtils.invokeAndWait(new Runnable() {

               @Override
               public void run() {
                  startup(null, virtualStack);
               }
            });
         } catch (InterruptedException ex) {
            ReportingUtils.logError(ex);
         } catch (InvocationTargetException ex) {
            ReportingUtils.logError(ex);
         }

      }
      hyperImage_.show();
      hyperImage_.getWindow().toFront();
   }
   
   public void show() {
      show(null);
   }

   public int getNumChannels() {
      return hyperImage_ == null ? 1 : ((IMMImagePlus) hyperImage_).getNChannelsUnverified();
   }

   public int getNumGrayChannels() {
      return getNumChannels();
   }

   public void setWindowTitle(String name) {
      name_ = name;
      updateWindowTitleAndStatus();
   }

   public void displayStatusLine(String status) {
      controls_.setStatusLabel(status);
   }

   public void setChannelContrast(int channelIndex, int min, int max, double gamma) {
      histograms_.setChannelContrast(channelIndex, min, max, gamma);
      histograms_.applyLUTToImage();
      drawWithoutUpdate();
   }
   
   public void updateChannelNamesAndColors() {
      if (histograms_ != null && histograms_ instanceof MultiChannelHistograms) {
         ((MultiChannelHistograms) histograms_).updateChannelNamesAndColors();
      }
   }
   
   public void setChannelHistogramDisplayMax(int channelIndex, int histMax) {
      histograms_.setChannelHistogramDisplayMax(channelIndex, histMax);
   }

   /*
    * Called just before image is drawn.  Notifies metadata panel to update
    * metadata or comments if this display is the active window.  Notifies histograms
    * that image is change to create appropriate LUTs and to draw themselves if this
    * is the active window
    */
   private void imageChangedUpdate() {
      boolean updatePixelSize = updatePixelSize_.get();

      if (updatePixelSize) {
         try {
            JSONObject summary = getSummaryMetadata();
            if (summary != null) {
               summary.put("PixelSize_um", Double.longBitsToDouble(newPixelSize_.get()));
            }
            if (hyperImage_ != null) {
               applyPixelSizeCalibration(hyperImage_);
            }
            
         } catch (JSONException ex) {
            ReportingUtils.logError("Error in imageChangedUpdate in VirtualAcquisitionDisplay.java");
         } 
         updatePixelSize_.set(false);
      } else {
         if (hyperImage_ != null) {
            Calibration cal = hyperImage_.getCalibration();
            double calPixSize = cal.pixelWidth;
            JSONObject tags = this.getCurrentMetadata();
            if (tags != null) {
               try {
                  double imgPixSize = tags.getDouble("PixelSizeUm");
                  if (calPixSize != imgPixSize) {
                     applyPixelSizeCalibration(hyperImage_);
                  }
               } catch (JSONException ex) {
                  ReportingUtils.logError("Found Image without PixelSizeUm tag");
               }
            }
         }
      }
      if (histograms_ != null) {
         histograms_.imageChanged();
      }      
      if (isActiveDisplay()) {         
         mdPanel_.imageChangedUpdate(this);
         if (updatePixelSize) {
            mdPanel_.redrawSizeBar();
         }
      }      
      imageChangedWindowUpdate(); //used to update status line
   }
   
   public boolean isActiveDisplay() {
      if (hyperImage_ == null || hyperImage_.getWindow() == null)
           return false;
       return hyperImage_.getWindow() == mdPanel_.getCurrentWindow();
   }

   /*
    * Called when contrast changes as a result of user or programmtic input, but underlying pixels 
    * remain unchanges
    */
   public void drawWithoutUpdate() {
      if (hyperImage_ != null) {
         ((IMMImagePlus) hyperImage_).drawWithoutUpdate();
      }
   }
   
   private void makeHistograms() {
      if (getNumChannels() == 1 )
           histograms_ = new SingleChannelHistogram(this);
       else
           histograms_ = new MultiChannelHistograms(this);
   }
   
   public Histograms getHistograms() {
       return histograms_;
   }
   
   public HistogramControlsState getHistogramControlsState() {
       return histogramControlsState_;
   }
   
   public void disableAutoStretchCheckBox() {
       if (isActiveDisplay() ) {
          mdPanel_.getContrastPanel().disableAutostretch();
       } else {
          histogramControlsState_.autostretch = false;
       }
   }
   
   public ContrastSettings getChannelContrastSettings(int channel) {
      return histograms_.getChannelContrastSettings(channel);
   }           
}
/* DisplayOpenGLView.h: Implementation for the DisplayOpenGLView class
   Copyright (c) 2006-2007 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

   Author contact information:

   E-mail: fredm@spamcop.net

*/

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <libspectrum.h>

#include "input.h"
#include "machines/specplus3.h"
#include "peripherals/disk/beta.h"
#include "ui/cocoa/cocoadisplay.h"
#include "ui/ui.h"

#define MAX_SCREEN_BUFFERS 2

@class Emulator;
@class Texture;

@interface DisplayOpenGLView : MTKView <MTKViewDelegate>
{
  /* Two CPU-side screen buffers; the dirty machinery in
     -uploadDirtyToTexture keeps both consistent across frames. */
  Cocoa_Texture screenTex[MAX_SCREEN_BUFFERS];
  /* Matching MTLTextures uploaded to the GPU. */
  id<MTLTexture> screenTexMTL[MAX_SCREEN_BUFFERS];
  int currentScreenTex;

  Texture *redCassette;
  Texture *greenCassette;
  Texture *redMdr;
  Texture *greenMdr;
  Texture *redDisk;
  Texture *greenDisk;

  BOOL screenTexInitialised;

  ui_statusbar_state disk_state;
  ui_statusbar_state mdr_state;
  ui_statusbar_state tape_state;

  NSLock *view_lock;

  float target_ratio;

  Emulator *real_emulator;
  Emulator *proxy_emulator;
  NSConnection *kitConnection;

  /* Metal rendering state. */
  id<MTLDevice> mtlDevice;
  id<MTLCommandQueue> commandQueue;
  id<MTLRenderPipelineState> pipelineState;
  /* Reusable CPU-side staging buffer for the BGR5A1 -> BGRA8 conversion
     done at upload time. Sized to a full screen on first use. */
  uint32_t *conversionBuffer;
  size_t conversionBufferPixels;

  /* Frame pacing is driven by MTKView's built-in CADisplayLink, which
     calls -drawInMTKView: each vsync. -displayLinkStart / -displayLinkStop
     toggle self.paused (always on the main thread). */
}
+(DisplayOpenGLView *) instance;

-(IBAction) zoom:(id)sender;

-(void) createTexture:(Cocoa_Texture*)newScreen;
-(void) destroyTexture;

-(void) setServer:(id)anObject;
-(id) initWithFrame:(NSRect)frameRect;
-(void) awakeFromNib;

-(NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender;
-(BOOL)performDragOperation:(id <NSDraggingInfo>)sender;

-(void) loadPicture:(NSString *) name
           greenTex:(Texture*) greenTexture
             redTex:(Texture*) redTexture
            xOrigin:(int) x
            yOrigin:(int) y;

-(void) openFile:(const char *)filename;
-(void) snapOpen:(const char *)filename;
-(void) tapeOpen:(const char *)filename;
-(void) tapeWrite:(const char *)filename;
-(void) tapeTogglePlay;
-(void) tapeToggleRecord;
-(void) tapeRewind;
-(void) tapeClear;
-(int) tapeClose;
-(void) tapeWindowInitialise;
-(void) cocoaBreak;
-(void) pause;
-(void) unpause;
-(void) reset;
-(void) hard_reset;
-(void) nmi;
-(int) checkMediaChanged;

-(void) diskInsertNew:(int)which;
-(void) diskInsert:(const char *)filename inDrive:(int)which;
-(void) diskEject:(int)drive;
-(void) diskSave:(int)drive saveAs:(bool)saveas;
-(int) diskWrite:(int)drive saveAs:(bool)saveas;
-(void) diskFlip:(int)drive side:(int)flip;
-(void) diskWriteProtect:(int)which protect:(int)write;

-(void) snapshotWrite:(const char *)filename;

-(void) screenshotScrRead:(const char *)filename;
-(void) screenshotScrWrite:(const char *)filename;
-(void) screenshotWrite:(const char *)filename;

-(void) profileStart;
-(void) profileFinish:(const char *)filename;

-(void) settingsSave;
-(void) settingsResetDefaults;

-(void) joystickToggleKeyboard;
-(void) keyboardToggleRecreatedZXSpectrum;
-(void) keyboardToggleArrowsShifted;

-(int) rzxStartPlayback:(const char *)filename;
-(void) rzxInsertSnap;
-(void) rzxRollback;
-(int) rzxStartRecording:(const char *)filename embedSnapshot:(int)flag;
-(void) rzxStop;
-(int) rzxContinueRecording:(const char *)filename;
-(int) rzxFinaliseRecording:(const char *)filename;

-(void) movieStartRecording:(const char *)filename;
-(void) movieTogglePause;
-(void) movieStop;

-(void) didaktik80Snap;

-(void) multifaceRedButton;

-(void) if1MdrNew:(int)drive;
-(void) if1MdrInsert:(const char *)filename inDrive:(int)drive;
-(void) if1MdrCartEject:(int)drive;
-(void) if1MdrCartSave:(int)drive saveAs:(bool)saveas;
-(void) if1MdrWriteProtect:(int)w inDrive:(int)drive;

-(int) if2Insert:(const char *)filename;
-(void) if2Eject;

-(int) dckInsert:(const char *)filename;
-(void) dckEject;

-(void) psgStart:(const char *)psgfile;
-(void) psgStop;

-(int) simpleideInsert:(const char *)filename inUnit:(libspectrum_ide_unit)unit;
-(int) simpleideCommit:(libspectrum_ide_unit)unit;
-(int) simpleideEject:(libspectrum_ide_unit)unit;

-(int) zxataspInsert:(const char *)filename inUnit:(libspectrum_ide_unit)unit;
-(int) zxataspCommit:(libspectrum_ide_unit)unit;
-(int) zxataspEject:(libspectrum_ide_unit)unit;

-(int) zxcfInsert:(const char *)filename;
-(int) zxcfCommit;
-(int) zxcfEject;

-(int) divideInsert:(const char *)filename inUnit:(libspectrum_ide_unit)unit;
-(int) divideCommit:(libspectrum_ide_unit)unit;
-(int) divideEject:(libspectrum_ide_unit)unit;

-(int) divmmcInsert:(const char *)filename;
-(int) divmmcCommit;
-(int) divmmcEject;

-(int) zxmmcInsert:(const char *)filename;
-(int) zxmmcCommit;
-(int) zxmmcEject;

-(void) setDiskState:(NSNumber*)state;
-(void) setTapeState:(NSNumber*)state;
-(void) setMdrState:(NSNumber*)state;

-(ui_confirm_save_t) confirmSave:(NSString*)theMessage;
-(int) confirm:(NSString*)theMessage;
-(int) tapeWrite;
-(int) diskWrite:(int)which saveAs:(bool)saveas;
-(int) if1MdrWrite:(int)which saveAs:(bool)saveas;
-(ui_confirm_joystick_t) confirmJoystick:(libspectrum_joystick)type inputs:(int)theInputs;

-(void) debuggerActivate;

-(void) mouseMoved:(NSEvent *)theEvent;
-(void) mouseDown:(NSEvent *)theEvent;
-(void) mouseUp:(NSEvent *)theEvent;
-(void) rightMouseDown:(NSEvent *)theEvent;
-(void) rightMouseUp:(NSEvent *)theEvent;
-(void) otherMouseDown:(NSEvent *)theEvent;
-(void) otherMouseUp:(NSEvent *)theEvent;

-(void) flagsChanged:(NSEvent *)theEvent;
-(void) keyDown:(NSEvent *)theEvent;
-(void) keyUp:(NSEvent *)theEvent;

-(BOOL) acceptsFirstResponder;
-(BOOL) becomeFirstResponder;
-(BOOL) resignFirstResponder;

-(BOOL) isFlipped;

-(BOOL) windowShouldClose:(id)window;
-(void) windowDidResignKey:(NSNotification *)notification;
-(void) windowDidEnterFullScreen:(NSNotification *)notification;
-(void) windowDidExitFullScreen:(NSNotification *)notification;

-(void) windowChangedScreen:(NSNotification*)inNotification;

-(void) displayLinkStop;
-(void) displayLinkStart;

@end

/* Helper category used by every utility controller in §Affected Windows
   (spec 004) to pin its panel to a parent window and reverse that
   relationship at teardown. Centralizing the reconciliation removes
   the previously-duplicated 7-line block in seven controllers. */
@interface NSWindow (FuseChildPinning)
/* Make `self` a child window of `parent` with NSWindowAbove ordering.
   If `self` is already a child of `parent`, this is a no-op. If `self`
   has a different parent, it is detached first. Passing `nil` or
   `self` for `parent` is a no-op. */
- (void)pinAsChildOf:(NSWindow *)parent;
/* Remove `self` from whatever parent it currently has. No-op if there
   is no parent. */
- (void)unpinFromParent;
@end

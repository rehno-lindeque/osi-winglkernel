//////////////////////////////////////////////////////////////////////////////
//
//    API.CPP
//
//    Copyright � 2007, Rehno Lindeque. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////////
/*                               DOCUMENTATION                              */
/*    
    DESCRIPTION:
      WinGLKernel api implementation

    TODO: 
      + Add the new update method. Enable the api to create its own 
        default handler if desired (i.e. the way it is currently).
*/
/*                              COMPILER MACROS                             */
#pragma warning(push)
#pragma warning(disable:4311) // Type cast: Pointer truncation
#pragma warning(disable:4312) // Type cast: Conversion to greater size (pointer)
#pragma warning(disable:4390) // Empty controlled statement
#pragma warning(disable:4244) // Conversion from float to int (possible loss of data)

/*                                 INCLUDES                                 */
#include "api.h"
#include <gl/gl.h>

// STL headers
#include <vector>

/*                              COMPILER MACROS                             */
#define _this (*(WinGLKernel::Kernel*)this)

#define cast_id     BaseKernel::Kernel::cast_id
#define cast_object BaseKernel::Kernel::cast_object

/*                                  GLOBALS                                 */
std::vector<DISPLAY_DEVICE> displayDevices;
std::vector<WinGLKernelDisplayObject*> displayObjects;
std::vector<WinGLKernel::DisplayChain::DisplayInstance> displayChainList;

HGLRC oldHRC;
HDC oldHDC;
HINSTANCE hInstance;

/*                               IMPLEMENTATION                             */

HGLRC createRenderContext(HWND hWnd)
{
  /* PFD_SWAP_EXCHANGE flag alows the back buffer to be swapped rather than copied
     Also, this is important for SLI cards (see GPU_Programming_Guide.pdf by nvidia) */

  // Store the original contexts (needed?)
  oldHRC = wglGetCurrentContext();
  oldHDC = wglGetCurrentDC();
  
  // Get & store the window's device context
  HDC hDC = ::GetDC(hWnd);

  if(!hDC)
    return null;
 
  // Choose an appropriate pixel format
  int pixelFormat;

  static PIXELFORMATDESCRIPTOR PFD =
  { 
    sizeof(PIXELFORMATDESCRIPTOR),
    1,
    PFD_DRAW_TO_WINDOW |
    PFD_SUPPORT_OPENGL |
    PFD_SWAP_EXCHANGE | //PFD_SWAP_COPY
    PFD_DOUBLEBUFFER,
    PFD_TYPE_RGBA,
    32,
    0, 0, 0, 0, 0, 0,
    0,
    0,
    0,
    0, 0, 0, 0,
    16,
    0,
    0,
    PFD_MAIN_PLANE,
    0,
    0, 0, 0
  };

  if(!(pixelFormat = ChoosePixelFormat(hDC, &PFD)))
  {
    ReleaseDC(hWnd, hDC);
    return null;
  }

  if(!SetPixelFormat(hDC, pixelFormat, &PFD))
  {
    ReleaseDC(hWnd, hDC);
    return null;  
  }

  // Create rendering context
  HGLRC hRC;

  if(!(hRC = wglCreateContext(hDC)))
  {
    ReleaseDC(hWnd, hDC);
    return null;
  }

  if(!(wglMakeCurrent(hDC, hRC)))
  {
    wglMakeCurrent(oldHDC, oldHRC);
    wglDeleteContext(hRC);
    ReleaseDC(hWnd, hDC);
    return null;
  }

  return hRC;
}

OSobject OSI_API_CALL Kernel::beginDisplay(OSuint displayNumber, const OSstring name, OSbool fullScreen, OSuint width, OSuint height)
{
  DISPLAY_DEVICE* displayDevice;
  uint32 style, exStyle;

  // Get display device
  if(displayNumber >= displayDevices.size())
    return 0; // error
  else
    displayDevice = &displayDevices[displayNumber];

  // Change display settings
  if(fullScreen)
  {
    // Determine screenmode
    DEVMODE screenMode;
    /*EnumDisplaySettings(displayDevice->DeviceName, ENUM_CURRENT_SETTINGS, &screenMode);*/
    
    memset(&screenMode, 0, sizeof(screenMode));
    screenMode.dmSize       = sizeof(screenMode);
    //memcpy(screenMode.dmDeviceName, displayDevice->DeviceName, sizeof(CHAR[32]));
    screenMode.dmPelsWidth  = width;
    screenMode.dmPelsHeight = height;
    screenMode.dmBitsPerPel = 32;
    screenMode.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

    if(ChangeDisplaySettingsEx(displayDevice->DeviceName, &screenMode, null, CDS_FULLSCREEN, null) != DISP_CHANGE_SUCCESSFUL)
      fullScreen = false;
  }

  // Set up window
  int xPosition, yPosition;
  style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
  exStyle = 0;

  if(fullScreen)
  {
    // Hide cursor
    //ShowCursor(false); //todo: move this...

    // Window styles
    style   |= WS_POPUP;
    exStyle |= WS_EX_APPWINDOW;

    // Calculate window position (on the display)
    DEVMODE screenMode;

    EnumDisplaySettings(displayDevice->DeviceName, ENUM_CURRENT_SETTINGS, &screenMode);

    xPosition = screenMode.dmPosition.x;
    yPosition = screenMode.dmPosition.y;
  }
  else
  {
    // Window styles
    style   |= WS_OVERLAPPEDWINDOW;
    exStyle |= WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    
    // Calculate window position (centered)
    DEVMODE screenMode;

    EnumDisplaySettings(displayDevice->DeviceName, ENUM_CURRENT_SETTINGS, &screenMode);

    xPosition = screenMode.dmPosition.x + ((screenMode.dmPelsWidth - width)>>1);
    yPosition = screenMode.dmPosition.y + ((screenMode.dmPelsHeight-height)>>1);
  }

  //// Create window
  // Set up window rectangle
  RECT windowRect = { xPosition, yPosition, xPosition+width, yPosition+height };
  AdjustWindowRectEx(&windowRect, style, false, exStyle);

  // Register window class
  char className[15] = "OPENGL_WINDOW0";
  className[13] += displayNumber;

  WNDCLASSEX wndClass;
  memset(&wndClass, 0, sizeof(wndClass));
  wndClass.cbSize         = sizeof(wndClass);
  wndClass.hInstance      = hInstance;
  //todo: ? wndClass.cbClsExtra     = sizeof(OSobject);
  wndClass.cbClsExtra     = 0;
  wndClass.style	        = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wndClass.lpfnWndProc    = (WNDPROC)WinGLKernel::Kernel::windowProc;
  wndClass.hIcon	        = LoadIcon(NULL, IDI_WINLOGO);
  wndClass.hCursor	      = LoadCursor(NULL, IDC_ARROW);
  wndClass.lpszClassName  = className;

  if(RegisterClassEx(&wndClass) == 0)
  {
#ifdef _DEBUG
    DWORD error = GetLastError();
#endif
    return 0; // error
  }

  // Create window
  HWND hWnd = CreateWindowEx(exStyle,
                             wndClass.lpszClassName,
                             name,
                             style,
                             windowRect.left,
                             windowRect.top,
                             windowRect.right-windowRect.left,
                             windowRect.bottom-windowRect.top,
                             null,
                             null,
                             wndClass.hInstance,
                             null);
  if(hWnd == null)
  {
#ifdef _DEBUG
    DWORD error = GetLastError();
#endif
    return 0; //error
  }

  //todo: ? SetWindowLong(hWnd, GWL_USERDATA, (uint32)...);

  // Show window
  ShowWindow(hWnd, SW_SHOW);
  SetForegroundWindow(hWnd);
  SetFocus(hWnd);
  
  // Create a rendering context
  HGLRC hRC = createRenderContext(hWnd);

  //// Create display object
  WinGLKernelDisplayObject& displayObject = *new WinGLKernelDisplayObject;
  displayObject.hWnd = hWnd;
  displayObject.hRC = hRC;
  displayObject.x1 = 0; 
  displayObject.y1 = 0;
  displayObject.x2 = width;
  displayObject.y2 = height;

  displayObjects.push_back(&displayObject);

  return (OSobject)&displayObject;
}

void OSI_API_CALL Kernel::endDisplay()
{
}


OSobject OSI_API_CALL Kernel::beginDisplayChain()
{
  WinGLKernel::DisplayChain* displayChainObject = _this.beginObject<WinGLKernel::DisplayChain>();
  displayChainList.reserve(10);
  return cast_object(displayChainObject);  
}

void OSI_API_CALL Kernel::endDisplayChain()
{
  WinGLKernel::DisplayChain& displayChainObject = *_this.endObject<WinGLKernel::DisplayChain>();

  displayChainObject.nDisplayInstances = (OSuint32)displayChainList.size();
  displayChainObject.displayInstances = new WinGLKernel::DisplayChain::DisplayInstance[displayChainObject.nDisplayInstances];

  // Note: This can be done more efficiently by copying the entire vector at once
  //for(std::vector<WinGLKernel::DisplayChain::DisplayInstance>::iterator i = displayChainList.begin(); i != displayChainList.end(); ++i)
  for(uint c = 0; c < displayChainObject.nDisplayInstances; ++c)
    memcpy(&displayChainObject.displayInstances[c], &displayChainList[c], sizeof(WinGLKernel::DisplayChain::DisplayInstance));

  displayChainList.clear();
}

void OSI_API_CALL Kernel::display(OSobject display, OSfloat x1, OSfloat y1, OSfloat x2, OSfloat y2)
{
  displayChainList.resize(displayChainList.size()+1);
  WinGLKernel::DisplayChain::DisplayInstance& displayInstance = displayChainList.back();
  displayInstance.display = display;
  displayInstance.x1 = x1;
  displayInstance.y1 = y1;
  displayInstance.x2 = x2;
  displayInstance.y2 = y2;
}

void OSI_API_CALL Kernel::refreshDisplay(OSobject display)
{
  if(display == 0)
  {
    // Refresh all displays
    for(std::vector<WinGLKernelDisplayObject*>::iterator i = displayObjects.begin(); i != displayObjects.end(); ++i)
    {      
      WinGLKernelDisplayObject& displayObject = **i;
      HDC hDC = GetDC(displayObject.hWnd);
      SwapBuffers(hDC);
    }
  }
  else
  {
    // Refresh display
    WinGLKernelDisplayObject& displayObject = *(WinGLKernelDisplayObject*)display;
    HDC hDC = GetDC(displayObject.hWnd);
    SwapBuffers(hDC);
  }
}

OSobject OSI_API_CALL Kernel::beginFrame(OSobject displayChain, OSobject parentFrame)
{
  WinGLKernel::Frame* frameObject = _this.beginObject<WinGLKernel::Frame>();
  return cast_object(frameObject);
}

void OSI_API_CALL Kernel::endFrame()
{
  WinGLKernel::Frame* frameObject = _this.endObject<WinGLKernel::Frame>();
}

void OSI_API_CALL Kernel::openFrame(OSobject frame)
{
  WinGLKernel::Frame* frameObject = _this.openObject<WinGLKernel::Frame>(frame);
}

void OSI_API_CALL Kernel::closeFrame()
{
  WinGLKernel::Frame* frameObject = _this.closeObject<WinGLKernel::Frame>();
}

void OSI_API_CALL Kernel::frameArea(OSfloat x1, OSfloat y1, OSfloat x2, OSfloat y2)
{
  WinGLKernel::Frame& frameObject = *_this.getActiveObject<WinGLKernel::Frame>();
  frameObject.setArea(x1, y1, x2, y2);
}

void OSI_API_CALL Kernel::loadDynamicLibrary(OSstring path)
{
  //todo: return OSobject?
  //todo
}

void* OSI_API_CALL Kernel::getIntKernel()
{
  return _this.intKernel;
}

const OSstring OSI_API_CALL Kernel::getVersion()
{
  return KERNEL_VERSION;
}

void OSI_API_CALL Kernel::shutdown()
{
  // Remove all remaining messages from the message queue
  {
	  MSG msg;
	  while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		  DispatchMessage(&msg);
  }

  // Restore previous OpenGL Render Contex and GDI Device Context (needed?)
  wglMakeCurrent(oldHDC, oldHRC);

  // Destroy displays
  uint c = 0;
  for(std::vector<WinGLKernelDisplayObject*>::iterator i = displayObjects.begin(); i != displayObjects.end(); ++i)
  {
    WinGLKernelDisplayObject& displayObject = **i;
    
    // Destroy render contexts
    wglDeleteContext(displayObject.hRC);
    displayObject.hRC = 0;
    
    // Unregister and destroy windows
    if(displayObject.hWnd)
	  {
		  if(!DestroyWindow(displayObject.hWnd)); // error

      displayObject.hWnd = 0;
	  }
    
    char className[15] = "OPENGL_WINDOW0";
    className[13] += c;

    if(!UnregisterClass(className, hInstance)); // error

    ++c;

    // Free display object memory
    delete &displayObject;
  }
  displayObjects.clear();

  // Return to original screen mode
  ChangeDisplaySettings(null, 0);
}

const void* OSI_API_CALL IntKernel::openClipFrames(OSobject displayChain, OSobject frame)
{
  /*int intKernelOffset = (int)&(((WinGLKernel::Kernel*)null)->intKernel);
  WinGLKernel::Kernel* kernel = (WinGLKernel::Kernel*)(((uint8*)(WinGLKernel::IntKernel*)this) - intKernelOffset);*/
  
  static OSuint8 buffer[sizeof(OSuint32) + sizeof(IntKernel::ClipFrame)*10];
  
  OSuint32& bufferSize = *(OSuint32*)buffer;
  IntKernel::ClipFrame* clipFrames = (IntKernel::ClipFrame*)(buffer+sizeof(OSuint32));
  
  WinGLKernel::DisplayChain& displayChainObject = *cast_id<WinGLKernel::DisplayChain>(displayChain);
  WinGLKernel::Frame&        frameObject        = *cast_id<WinGLKernel::Frame>(frame);

  
  // Test for frame intersection with all displays in the display chain
  for(uint c = 0; c < displayChainObject.nDisplayInstances; ++c)
  {
    WinGLKernel::DisplayChain::DisplayInstance& displayInstance = displayChainObject.displayInstances[c];
    IntKernel::ClipFrame& clipFrame = clipFrames[c];
    
    // Test intersection
    if(  frameObject.area.x1 > displayInstance.x2
      || frameObject.area.x2 < displayInstance.x1
      || frameObject.area.y1 > displayInstance.y2
      || frameObject.area.y2 < displayInstance.y1)
      continue;

    //// Calculate clip frame
    // Retrieve the display's "pixel" space determinants
    WinGLKernelDisplayObject& displayObject = *(WinGLKernelDisplayObject*)displayInstance.display;
    
    OSfloat xDet = (displayObject.x2 - displayObject.x1) / (displayInstance.x2 - displayInstance.x1);
    OSfloat yDet = (displayObject.y2 - displayObject.y1) / (displayInstance.y2 - displayInstance.y1);

    // Get clipped coordinates (and translate to unnormalized display space)
    /*OSfloat x1 = max(frameObject.area.x1, displayInstance.x1),
            y1 = max(frameObject.area.y1, displayInstance.y1),
            x2 = min(frameObject.area.x2, displayInstance.x2),
            y2 = min(frameObject.area.y2, displayInstance.y2);*/
    
    OSfloat x1 = max(frameObject.area.x1 - displayInstance.x1, 0.0f),
            y1 = max(frameObject.area.y1 - displayInstance.y1, 0.0f),
            x2 = min(frameObject.area.x2, displayInstance.x2) - displayInstance.x1,
            y2 = min(frameObject.area.y2, displayInstance.y2) - displayInstance.y1;

    // Convert to pixel coordinates
    clipFrame.display = displayInstance.display;
    clipFrame.x1 = (OSuint)x1*xDet;
    clipFrame.y1 = (OSuint)y1*yDet;
    clipFrame.x2 = (OSuint)x2*xDet;
    clipFrame.y2 = (OSuint)y2*yDet;
  }

  return buffer;
}

void OSI_API_CALL IntKernel::closeClipFrames()
{
}

const void* OSI_API_CALL IntKernel::openDisplays(OSobject displayChain)
{
  static OSuint8 buffer[sizeof(OSuint32) + sizeof(OSobject)*10];
  
  OSuint32& bufferSize = *(OSuint32*)buffer;
  OSobject* displays = (OSobject*)(buffer+sizeof(OSuint32));
  
  WinGLKernel::DisplayChain& displayChainObject = *cast_id<WinGLKernel::DisplayChain>(displayChain);

  bufferSize = displayChainObject.nDisplayInstances;
  
  for(uint c = 0; c < bufferSize; ++c)
    displays[c] = displayChainObject.displayInstances[c].display;

  return buffer;
}

void OSI_API_CALL IntKernel::closeDisplays()
{
}

Kernel* OSI_API_CALL kernelInit()
{
  // Get module instance
  hInstance = GetModuleHandle(null);

  // Get all display devices
  DISPLAY_DEVICE displayDevice;
  
  int c = 0;
  while(true)
  {
    displayDevice.cb = sizeof(DISPLAY_DEVICE);

    if(EnumDisplayDevices(null, c, &displayDevice, 0) == 0)
      break;
    
    if(displayDevice.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
    {
      if(displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
      {
        if(displayDevices.empty())
          displayDevices.push_back(displayDevice);
        else
          memcpy(&displayDevices.front(), &displayDevice, sizeof(displayDevice));
      }
      else
      {
        if(displayDevices.empty())
        {
          DISPLAY_DEVICE emptyDisplayDevice;
          memset(&emptyDisplayDevice, 0, sizeof(emptyDisplayDevice));
          displayDevices.push_back(emptyDisplayDevice);
        }
        displayDevices.push_back(displayDevice);
      }
    }

    ++c;
  }

  // Create kernel object
  WinGLKernel::Kernel* kernelObject = new WinGLKernel::Kernel;
  kernelObject->intKernel = new WinGLKernel::IntKernel;

  return (::Kernel*)kernelObject;
}

/*                              COMPILER MACROS                             */
#pragma warning(pop)

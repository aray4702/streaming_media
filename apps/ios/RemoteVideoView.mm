// The remote video on the GPU (#1133); see RemoteVideoView.h. Follows libwebrtc's sdk/objc RTCMTLVideoView,
// RTCMTLRenderer and RTCMTLI420Renderer (not in compare/webrtc/reference; the design and constants are theirs).
#import "RemoteVideoView.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

namespace {

// RTCMTLI420Renderer's shaders, compiled at run time as libwebrtc does (newLibraryWithSource).
NSString* const kShaderSource = @R"(
#include <metal_stdlib>
using namespace metal;

typedef struct {
  packed_float2 position;
  packed_float2 texcoord;
} Vertex;

typedef struct {
  float4 position [[position]];
  float2 texcoord;
} Varyings;

vertex Varyings vertexPassthrough(constant Vertex* verticies [[buffer(0)]], unsigned int vid [[vertex_id]]) {
  Varyings out;
  constant Vertex& v = verticies[vid];
  out.position = float4(float2(v.position), 0.0, 1.0);
  out.texcoord = v.texcoord;
  return out;
}

fragment half4 fragmentColorConversion(Varyings in [[stage_in]],
                                       texture2d<float, access::sample> textureY [[texture(0)]],
                                       texture2d<float, access::sample> textureU [[texture(1)]],
                                       texture2d<float, access::sample> textureV [[texture(2)]]) {
  constexpr sampler s(address::clamp_to_edge, filter::linear);
  float y = textureY.sample(s, in.texcoord).r;
  float u = textureU.sample(s, in.texcoord).r - 0.5;
  float v = textureV.sample(s, in.texcoord).r - 0.5;
  // BT.601 full range (the VideoToolbox decoder's output, videotoolbox_h264.h), as RTCMTLI420Renderer.
  float r = y + 1.403 * v;
  float g = y - 0.344 * u - 0.714 * v;
  float b = y + 1.770 * u;
  return half4(float4(r, g, b, 1.0));
}
)";

// RTCMTLRenderer's getCubeVertexData without cropping: a full-screen triangle strip (x, y, u, v), with the texture
// coordinates turned by the frame's clockwise rotation.
void QuadVertices(sm::video::Rotation rotation, float out[16]) {
  static const float k0[16] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
  static const float k90[16] = {-1, -1, 1, 1, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 0, 0};
  static const float k180[16] = {-1, -1, 1, 0, 1, -1, 0, 0, -1, 1, 1, 1, 1, 1, 0, 1};
  static const float k270[16] = {-1, -1, 0, 0, 1, -1, 0, 1, -1, 1, 1, 0, 1, 1, 1, 1};
  const float* v = rotation == sm::video::Rotation::k90    ? k90
                   : rotation == sm::video::Rotation::k180 ? k180
                   : rotation == sm::video::Rotation::k270 ? k270
                                                           : k0;
  for (int i = 0; i < 16; ++i) out[i] = v[i];
}

}  // namespace

@interface SMRemoteVideoView () <MTKViewDelegate>
@end

@implementation SMRemoteVideoView {
  MTKView* _metalView;
  id<MTLDevice> _device;
  id<MTLCommandQueue> _queue;
  id<MTLRenderPipelineState> _pipeline;
  id<MTLTexture> _y, _u, _v;
  dispatch_semaphore_t _inflight;  // One frame in flight, as RTCMTLRenderer's kMaxInflightBuffers.
  SMRemoteFrameSource _source;
  sm::video::I420Frame _frame;
  uint64_t _framesDrawn;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = UIColor.blackColor;
    _inflight = dispatch_semaphore_create(1);
    _device = MTLCreateSystemDefaultDevice();
    if (_device) [self setupMetal];
  }
  return self;
}

- (void)setupMetal {
  NSError* error = nil;
  id<MTLLibrary> library = [_device newLibraryWithSource:kShaderSource options:nil error:&error];
  if (!library) {
    NSLog(@"SmCall: Metal shaders: %@", error.localizedDescription);
    return;
  }
  MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
  desc.vertexFunction = [library newFunctionWithName:@"vertexPassthrough"];
  desc.fragmentFunction = [library newFunctionWithName:@"fragmentColorConversion"];
  desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  _pipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
  if (!_pipeline) {
    NSLog(@"SmCall: Metal pipeline: %@", error.localizedDescription);
    return;
  }
  _queue = [_device newCommandQueue];
  // RTCMTLVideoView: an MTKView that runs its own display-driven loop and draws only when a new frame arrived.
  _metalView = [[MTKView alloc] initWithFrame:self.bounds device:_device];
  _metalView.delegate = self;
  _metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  _metalView.clearColor = MTLClearColorMake(0, 0, 0, 1);
  _metalView.framebufferOnly = YES;
  _metalView.autoResizeDrawable = NO;  // The drawable is the frame's size; the layer scales it aspect-fit.
  _metalView.contentMode = UIViewContentModeScaleAspectFit;
  _metalView.backgroundColor = UIColor.blackColor;
  _metalView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  [self addSubview:_metalView];
}

- (void)setFrameSource:(SMRemoteFrameSource)source {
  _source = std::move(source);
}

- (uint64_t)framesDrawn {
  return _framesDrawn;
}

- (id<MTLTexture>)texture:(id<MTLTexture>)t width:(int)w height:(int)h {
  if (t && static_cast<int>(t.width) == w && static_cast<int>(t.height) == h) return t;
  MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                               width:w
                                                                              height:h
                                                                           mipmapped:NO];
  d.usage = MTLTextureUsageShaderRead;
  return [_device newTextureWithDescriptor:d];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawInMTKView:(MTKView*)view {
  if (!_source || !_pipeline) return;
  sm::video::Rotation rotation = sm::video::Rotation::k0;
  // Skip the draw when no new frame arrived (RTCMTLVideoView's lastFrameTimeNs check).
  if (!_source(&_frame, &rotation) || _frame.width <= 0 || _frame.height <= 0) return;
  const int w = _frame.width, h = _frame.height, cw = _frame.chroma_width(), ch = _frame.chroma_height();
  const bool sideways = rotation == sm::video::Rotation::k90 || rotation == sm::video::Rotation::k270;
  const CGSize size = sideways ? CGSizeMake(h, w) : CGSizeMake(w, h);
  if (!CGSizeEqualToSize(view.drawableSize, size)) view.drawableSize = size;

  dispatch_semaphore_wait(_inflight, DISPATCH_TIME_FOREVER);
  _y = [self texture:_y width:w height:h];
  _u = [self texture:_u width:cw height:ch];
  _v = [self texture:_v width:cw height:ch];
  if (!_y || !_u || !_v) {
    dispatch_semaphore_signal(_inflight);
    return;
  }
  // RTCMTLI420Renderer's uploadTexturesToRenderEncoder: each plane copied as is into an R8 texture.
  [_y replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:_frame.y.data() bytesPerRow:w];
  [_u replaceRegion:MTLRegionMake2D(0, 0, cw, ch) mipmapLevel:0 withBytes:_frame.u.data() bytesPerRow:cw];
  [_v replaceRegion:MTLRegionMake2D(0, 0, cw, ch) mipmapLevel:0 withBytes:_frame.v.data() bytesPerRow:cw];

  MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
  id<CAMetalDrawable> drawable = view.currentDrawable;
  if (!pass || !drawable) {
    dispatch_semaphore_signal(_inflight);
    return;
  }
  id<MTLCommandBuffer> cmd = [_queue commandBuffer];
  dispatch_semaphore_t inflight = _inflight;
  [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
    dispatch_semaphore_signal(inflight);
  }];
  id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
  float vertices[16];
  QuadVertices(rotation, vertices);
  [enc setRenderPipelineState:_pipeline];
  [enc setVertexBytes:vertices length:sizeof vertices atIndex:0];
  [enc setFragmentTexture:_y atIndex:0];
  [enc setFragmentTexture:_u atIndex:1];
  [enc setFragmentTexture:_v atIndex:2];
  [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  [enc endEncoding];
  [cmd presentDrawable:drawable];
  [cmd commit];
  ++_framesDrawn;
}

@end

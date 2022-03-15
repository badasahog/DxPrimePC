#pragma region("preprocessor")
#pragma region("imports")
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <shellapi.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <vector>
#include <complex>
#include <cmath>
#include <wincodec.h>
#include <zlib.h>
#include "importer.hpp"
#pragma endregion("imports")
#pragma region("linker directives")

#pragma comment(linker, "/DEFAULTLIB:D3d12.lib")
#pragma comment(linker, "/DEFAULTLIB:DXGI.lib")
#pragma comment(linker, "/DEFAULTLIB:D3DCompiler.lib")

#pragma endregion("linker directives")
#pragma region("preprocessor system checks")
#ifdef _DEBUG
#ifdef NDEBUG
#error debug flag conflict
#endif
#endif

#if !_HAS_CXX20
#error C++20 is required
#endif

#ifndef _M_AMD64
#error only x64 architectures are supported, please select x64 in your build configuration
#endif

#ifndef __has_cpp_attribute
#error critical macro __has_cpp_attribute not defined
#endif

#if !__has_include(<Windows.h>)
#error critital header Windows.h not found
#endif

#pragma endregion("preprocessor system checks")
#pragma region("macros")

#ifdef min
#pragma push_macro("min")
#undef min
#endif

#ifdef max
#pragma push_macro("max")
#undef max
#endif

#ifdef CreateWindow
#pragma push_macro("CreateWindow")
#undef CreateWindow
#endif

#ifdef _DEBUG
#define BDEBUG 1
#else
#define BDEBUG 0
#endif

#if __has_cpp_attribute(nodiscard) < 201603L
#define _NODISCARD [[nodiscard]]
#endif

#ifndef DECLSPEC_NOINLINE
#if _MSC_VER >= 1300
#define DECLSPEC_NOINLINE  __declspec(noinline)
#else
#define DECLSPEC_NOINLINE
#endif
#endif

#ifndef _NORETURN
#define _NORETURN [[noreturn]]
#endif

#pragma endregion("macros")
#pragma region("function macros")

#ifdef _DEBUG
#define ASSERT_SUCCESS(x) assert(SUCCEEDED(x))
#else
#define ASSERT_SUCCESS(x) x
#endif

#define THROW_ON_FAIL(x) \
	if (FAILED(x)){ \
		std::cout << std::dec << "[" << __LINE__ <<"]ERROR: " << std::hex << x << std::dec << std::endl; \
		throw std::exception(); \
	}

#pragma endregion("function macros")
#pragma endregion("preprocessor")

using Microsoft::WRL::ComPtr;


// Handle to the window
HWND hwnd = NULL;
// name of the window (not the title)
constexpr LPCTSTR WindowName = L"BzTutsApp";

// title of the window
constexpr LPCTSTR WindowTitle = L"Bz Window";

// width and height of the window
int Width = 800;
int Height = 600;

// is window full screen?
bool FullScreen = false;

// we will exit the program when this becomes false
bool Running = true;

// callback function for windows messages
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// direct3d stuff
constexpr int frameBufferCount = 3; // number of buffers we want, 2 for double buffering, 3 for tripple buffering

ComPtr<ID3D12Device> device; // direct3d device

ComPtr<IDXGISwapChain3> swapChain; // swapchain used to switch between render targets

ComPtr<ID3D12CommandQueue> commandQueue; // container for command lists

ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap; // a descriptor heap to hold resources like the render targets

ComPtr<ID3D12Resource> renderTargets[frameBufferCount]; // number of render targets equal to buffer count

ComPtr<ID3D12CommandAllocator> commandAllocator[frameBufferCount]; // we want enough allocators for each buffer * number of threads (we only have one thread)

ComPtr<ID3D12GraphicsCommandList> commandList; // a command list we can record commands into, then execute them to render the frame

ComPtr<ID3D12Fence> fence[frameBufferCount];    // an object that is locked while our command list is being executed by the gpu. We need as many 
										 //as we have allocators (more if we want to know when the gpu is finished with an asset)

HANDLE fenceEvent; // a handle to an event when our fence is unlocked by the gpu

UINT64 fenceValue[frameBufferCount]; // this value is incremented each frame. each fence will have its own value

int frameIndex; // current rtv we are on

int rtvDescriptorSize; // size of the rtv descriptor on the device (all front and back buffers will be the same size)
					   // function declarations

ComPtr<ID3D12PipelineState> pipelineStateObject; // pso containing a pipeline state

ComPtr<ID3D12RootSignature> rootSignature; // root signature defines data shaders will access

//D3D12_VIEWPORT viewport; // area that output from rasterizer will be stretched to.

//D3D12_RECT scissorRect; // the area to draw in. pixels outside that area will not be drawn onto

ComPtr<ID3D12Resource> vertexBuffer; // a default buffer in GPU memory that we will load vertex data for our triangle into
ComPtr<ID3D12Resource> indexBuffer; // a default buffer in GPU memory that we will load index data for our triangle into

//D3D12_VERTEX_BUFFER_VIEW vertexBufferView; // a structure containing a pointer to the vertex data in gpu memory
										   // the total size of the buffer, and the size of each element (vertex)

//D3D12_INDEX_BUFFER_VIEW indexBufferView; // a structure holding information about the index buffer

ComPtr<ID3D12Resource> depthStencilBuffer; // This is the memory for our depth buffer. it will also be used for a stencil buffer in a later tutorial
ComPtr<ID3D12DescriptorHeap> dsDescriptorHeap; // This is a heap for our depth/stencil buffer descriptor

// this is the structure of our constant buffer.
struct ConstantBufferPerObject {
	DirectX::XMFLOAT4X4 wvpMat;
};

// Constant buffers must be 256-byte aligned which has to do with constant reads on the GPU.
// We are only able to read at 256 byte intervals from the start of a resource heap, so we will
// make sure that we add padding between the two constant buffers in the heap (one for cube1 and one for cube2)
// Another way to do this would be to add a float array in the constant buffer structure for padding. In this case
// we would need to add a float padding[50]; after the wvpMat variable. This would align our structure to 256 bytes (4 bytes per float)
// The reason i didn't go with this way, was because there would actually be wasted cpu cycles when memcpy our constant
// buffer data to the gpu virtual address. currently we memcpy the size of our structure, which is 16 bytes here, but if we
// were to add the padding array, we would memcpy 64 bytes if we memcpy the size of our structure, which is 50 wasted bytes
// being copied.
constexpr int ConstantBufferPerObjectAlignedSize = (sizeof(ConstantBufferPerObject) + 255) & ~255;

ConstantBufferPerObject cbPerObject; // this is the constant buffer data we will send to the gpu 
										// (which will be placed in the resource we created above)

ComPtr<ID3D12Resource> constantBufferUploadHeaps[frameBufferCount]; // this is the memory on the gpu where constant buffers for each frame will be placed

UINT8* cbvGPUAddress[frameBufferCount]; // this is a pointer to each of the constant buffer resource heaps

DirectX::XMFLOAT4X4 cameraProjMat; // this will store our projection matrix
DirectX::XMFLOAT4X4 cameraViewMat; // this will store our view matrix

DirectX::XMFLOAT4 cameraPosition; // this is our cameras position vector
DirectX::XMFLOAT4 cameraTarget; // a vector describing the point in space our camera is looking at
DirectX::XMFLOAT4 cameraUp; // the worlds up vector

DirectX::XMFLOAT4X4 cube1WorldMat; // our first cubes world matrix (transformation matrix)
DirectX::XMFLOAT4X4 cube1RotMat; // this will keep track of our rotation for the first cube
DirectX::XMFLOAT4 cube1Position; // our first cubes position in space

DirectX::XMFLOAT4X4 cube2WorldMat; // our first cubes world matrix (transformation matrix)
DirectX::XMFLOAT4X4 cube2RotMat; // this will keep track of our rotation for the second cube
DirectX::XMFLOAT4 cube2PositionOffset; // our second cube will rotate around the first cube, so this is the position offset from the first cube

int numCubeIndices; // the number of indices to draw the cube

ComPtr<ID3D12Resource> textureBuffer; // the resource heap containing our texture

int LoadImageDataFromFile(BYTE** imageData, D3D12_RESOURCE_DESC& resourceDescription, const LPCWSTR&& filename, int& bytesPerRow);
DXGI_FORMAT GetDXGIFormatFromWICFormat(WICPixelFormatGUID& wicFormatGUID);
WICPixelFormatGUID GetConvertToWICFormat(WICPixelFormatGUID& wicFormatGUID);
int GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat);

ComPtr<ID3D12DescriptorHeap> mainDescriptorHeap;
ComPtr<ID3D12Resource> textureBufferUploadHeap;

struct Vertex {
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT2 texCoord;
	Vertex(const float& x, const float& y, const float& z, const float& u, const float& v) noexcept
		: pos(x, y, z), texCoord(u, v)
	{
	}
};

inline
void WaitForPreviousFrame() noexcept
{
	// swap the current rtv buffer index so we draw on the correct buffer
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// if the current fence value is still less than "fenceValue", then we know the GPU has not finished executing
	// the command queue since it has not reached the "commandQueue->Signal(fence, fenceValue)" command
	if (fence[frameIndex]->GetCompletedValue() < fenceValue[frameIndex])
	{
		// we have the fence create an event which is signaled once the fence's current value is "fenceValue"
		if (FAILED(fence[frameIndex]->SetEventOnCompletion(fenceValue[frameIndex], fenceEvent))) Running = false;
		// We will wait until the fence has triggered the event that it's current value has reached "fenceValue". once it's value
		// has reached "fenceValue", we know the command queue has finished executing
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// increment fenceValue for next frame
	fenceValue[frameIndex]++;
}


int main()
{

	const uint8_t* pak2Begin = indexPak(L"Metroid2.pak");

	loadAsset(pak2Begin, 0xD3D3AB81);


	// create the window
	if (FullScreen)
	{
		const HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfoW(hmon, &mi);

		Width = mi.rcMonitor.right - mi.rcMonitor.left;
		Height = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}

	const WNDCLASSEX wc =
	{
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.cbClsExtra = NULL,
		.cbWndExtra = NULL,
		.hInstance = GetModuleHandleW(nullptr),
		.hIcon = LoadIconW(nullptr, IDI_APPLICATION),
		.hCursor = LoadCursorW(nullptr, IDC_ARROW),
		.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2),
		.lpszMenuName = nullptr,
		.lpszClassName = WindowName,
		.hIconSm = LoadIconW(nullptr, IDI_APPLICATION)
	};

	if (!RegisterClassExW(&wc))
	{
		MessageBoxW(nullptr, L"Error registering class",
			L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	hwnd = CreateWindowExW(NULL,
		WindowName,
		WindowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		Width, Height,
		nullptr,
		nullptr,
		GetModuleHandleW(nullptr),
		nullptr);

	if (!hwnd)
	{
		MessageBoxW(nullptr, L"Error creating window",
			L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	if (FullScreen)
	{
		SetWindowLongW(hwnd, GWL_STYLE, 0);
	}

	ShowWindow(hwnd, 1);
	UpdateWindow(hwnd);


	// initialize direct3d

	// -- Create the Device -- //

	ComPtr<IDXGIFactory6> dxgiFactory;
	THROW_ON_FAIL(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> adapter; // adapters are the graphics card (this includes the embedded graphics on the motherboard or cpu)

	dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter4), &adapter);



	// Create the device
	THROW_ON_FAIL(D3D12CreateDevice(
		adapter.Get(),
		D3D_FEATURE_LEVEL_12_1,
		IID_PPV_ARGS(&device)
	));

	// -- Create a direct command queue -- //

	constexpr D3D12_COMMAND_QUEUE_DESC cqDesc = {
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
	};
	THROW_ON_FAIL(device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue))); // create the command queue


	// -- Create the Swap Chain (double/tripple buffering) -- //

	// describe our multi-sampling. We are not multi-sampling, so we set the count to 1 (we need at least one sample of course)


// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {
		.BufferDesc = {
			.Width = (UINT)Width,
			.Height = (UINT)Height,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM
		}, // our back buffer description
		.SampleDesc = {.Count = 1 }, // our multi-sampling description
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, // this says the pipeline will render to this swap chain
		.BufferCount = frameBufferCount, // number of buffers we have
		.OutputWindow = hwnd, // handle to our window
		.Windowed = !FullScreen, // set to true, then if in fullscreen must call SetFullScreenState with true for full screen to get uncapped fps
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD // dxgi will discard the buffer (data) after we call present
	};
	ComPtr<IDXGISwapChain> tempSwapChain;

	dxgiFactory->CreateSwapChain(
		commandQueue.Get(), // the queue will be flushed once the swap chain is created
		&swapChainDesc, // give it the swap chain description we created above
		&tempSwapChain // store the created swap chain in a temp IDXGISwapChain interface
	);

	tempSwapChain.As(&swapChain);

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// -- Create the Back Buffers (render target views) Descriptor Heap -- //

	// describe an rtv descriptor heap and create
	constexpr D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, // this heap is a render target view heap
		.NumDescriptors = frameBufferCount, // number of descriptors for this heap.
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
	};
	// This heap will not be directly referenced by the shaders (not shader visible), as this will store the output from the pipeline
	// otherwise we would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	THROW_ON_FAIL(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));

	// get the size of a descriptor in this heap (this is a rtv heap, so only rtv descriptors should be stored in it.
	// descriptor sizes may vary from device to device, which is why there is no set size and we must ask the 
	// device to give us the size. we will use this size to increment a descriptor handle offset
	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// get a handle to the first descriptor in the descriptor heap. a handle is basically a pointer,
	// but we cannot literally use it like a c++ pointer.
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Create a RTV for each buffer (double buffering is two buffers, tripple buffering is 3).
	for (int i = 0; i < frameBufferCount; i++)
	{
		// first we get the n'th buffer in the swap chain and store it in the n'th
		// position of our ID3D12Resource array
		THROW_ON_FAIL(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])));

		// the we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtv handle
		device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);

		// we increment the rtv handle by the rtv descriptor size we got above
		rtvHandle.ptr = (SIZE_T)((INT64)rtvHandle.ptr + 1i64 * (INT64)rtvDescriptorSize);
	}

	// -- Create the Command Allocators -- //

	for (int i = 0; i < frameBufferCount; i++)
	{
		THROW_ON_FAIL(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[i])));
	}

	// -- Create a Command List -- //

	// create the command list with the first allocator
	THROW_ON_FAIL(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[frameIndex].Get(), nullptr, IID_PPV_ARGS(&commandList)));


	// -- Create a Fence & Fence Event -- //

	// create the fences
	for (int i = 0; i < frameBufferCount; i++)
	{
		THROW_ON_FAIL(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence[i])));//todo: throwing?

		fenceValue[i] = 0; // set the initial fence value to 0
	}

	// create a handle to a fence event
	fenceEvent = CreateEventW(nullptr, false, false, nullptr);
	if (fenceEvent == nullptr)
	{
		Running = false;
		std::cout << "failed to create fence event" << std::endl;
		return false;
	}

	// create root signature

	// create a root descriptor, which explains where to find the data for this root parameter
	constexpr D3D12_ROOT_DESCRIPTOR rootCBVDescriptor =
	{
		.ShaderRegister = 0,
		.RegisterSpace = 0
	};

	// create a descriptor range (descriptor table) and fill it out
	// this is a range of descriptors inside a descriptor heap
	constexpr D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[1] = // only one range right now
	{
		{
			.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, // this is a range of shader resource views (descriptors)
			.NumDescriptors = 1, // we only have one texture right now, so the range is only 1
			.BaseShaderRegister = 0, // start index of the shader registers in the range
			.RegisterSpace = 0, // space 0. can usually be zero
			.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND // this appends the range to the end of the root signature descriptor tables
		}
	};
	// create a descriptor table
	const D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable =
	{
		.NumDescriptorRanges = _countof(descriptorTableRanges), // we only have one range
		.pDescriptorRanges = &descriptorTableRanges[0] // the pointer to the beginning of our ranges array
	};

	// create a root parameter for the root descriptor and fill it out
	// fill out the parameter for our descriptor table. Remember it's a good idea to sort parameters by frequency of change. Our constant
	// buffer will be changed multiple times per frame, while our descriptor table will not be changed at all (in this tutorial)
	const D3D12_ROOT_PARAMETER  rootParameters[2] = {
		{
			.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,// this is a constant buffer view root descriptor
			.Descriptor = rootCBVDescriptor, // this is the root descriptor for this root parameter
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX // our pixel shader will be the only shader accessing this parameter for now
		},
		{
			.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, // this is a descriptor table
			.DescriptorTable = descriptorTable, // this is our descriptor table for this root parameter
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL // our pixel shader will be the only shader accessing this parameter for now
		}
	}; // two root parameters



	// create a static sampler
	constexpr D3D12_STATIC_SAMPLER_DESC sampler = {
		.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
		.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		.MipLODBias = 0,
		.MaxAnisotropy = 0,
		.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
		.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
		.MinLOD = 0.0f,
		.MaxLOD = D3D12_FLOAT32_MAX,
		.ShaderRegister = 0,
		.RegisterSpace = 0,
		.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
	};//D3D12_ROOT_SIGNATURE_DESC
	const D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc =
	{
		.NumParameters = _countof(rootParameters),
		.pParameters = rootParameters,
		.NumStaticSamplers = 1,
		.pStaticSamplers = &sampler,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | // we can deny shader stages here for better performance
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
	};

	ComPtr<ID3DBlob> errorBuff; // a buffer holding the error data if any
	ComPtr<ID3DBlob> signature;
	THROW_ON_FAIL(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff));


	THROW_ON_FAIL(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));


	// create vertex and pixel shaders

	// when debugging, we can compile the shader files at runtime.
	// but for release versions, we can compile the hlsl shaders
	// with fxc.exe to create .cso files, which contain the shader
	// bytecode. We can load the .cso files at runtime to get the
	// shader bytecode, which of course is faster than compiling
	// them at runtime

	// compile vertex shader
	ComPtr<ID3DBlob> vertexShader; // d3d blob for holding vertex shader bytecode
	THROW_ON_FAIL(D3DCompileFromFile(L"VertexShader.hlsl",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vertexShader,
		&errorBuff));

	// compile pixel shader
	ComPtr<ID3DBlob> pixelShader;
	THROW_ON_FAIL(D3DCompileFromFile(L"PixelShader.hlsl",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&pixelShader,
		&errorBuff));

	// create input layout

	// The input layout is used by the Input Assembler so that it knows
	// how to read the vertex data bound to it.

	constexpr D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// fill out an input layout description structure
	// create a pipeline state object (PSO)

	// In a real application, you will have many pso's. for each different shader
	// or different combinations of shaders, different blend states or different rasterizer states,
	// different topology types (point, line, triangle, patch), or a different number
	// of render targets you will need a pso

	// VS is the only required shader for a pso. You might be wondering when a case would be where
	// you only set the VS. It's possible that you have a pso that only outputs data with the stream
	// output, and not on a render target, which means you would not need anything after the stream
	// output.
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { // a structure to define a pso
		.pRootSignature = rootSignature.Get(), // the root signature that describes the input data this pso needs
		.VS =
		{
			.pShaderBytecode = vertexShader->GetBufferPointer(),
			.BytecodeLength = vertexShader->GetBufferSize()
		}, // structure describing where to find the vertex shader bytecode and how large it is
		.PS =
		{
			.pShaderBytecode = pixelShader->GetBufferPointer(),
			.BytecodeLength = pixelShader->GetBufferSize()
		}, // same as VS but for pixel shader
		.BlendState =
		{
			.AlphaToCoverageEnable = false,
			.IndependentBlendEnable = false,
			.RenderTarget = {
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL},
				{false, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL}
			}
		}, // a default blent state.
		.SampleMask = 0xffffffff, // sample mask has to do with multi-sampling. 0xffffffff means point sampling is done
		.RasterizerState = // a default rasterizer state.
		{
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = D3D12_CULL_MODE_BACK,
			.FrontCounterClockwise = false,
			.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
			.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
			.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
			.DepthClipEnable = true,
			.MultisampleEnable = false,
			.AntialiasedLineEnable = false,
			.ForcedSampleCount = 0,
			.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
		},
		.DepthStencilState =
		{
			.DepthEnable = true,
			.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
			.StencilEnable = false,
			.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
			.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
			.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS },
			.BackFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS }
		},
		.InputLayout =
		{
			.pInputElementDescs = inputLayout, // we can get the number of elements in an array by "sizeof(array) / sizeof(arrayElementType)"
			.NumElements = sizeof(inputLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC) // the structure describing our input layout
		},
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, // type of topology we are drawing
		.NumRenderTargets = 1, // we are only binding one render target
		.RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM}, // format of the render target
		.SampleDesc = {.Count = 1, .Quality = 0} // must be the same sample description as the swapchain and depth/stencil buffer
	};
	// create the pso
	THROW_ON_FAIL(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));


	// Create vertex buffer

	// a cube
	Vertex vList[] = {
		// front face
		{ -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },
		{  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

		// right side face
		{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
		{  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

		// left side face
		{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

		// back face
		{  0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 1.0f, 0.0f },

		// top face
		{ -0.5f,  0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
		{  0.5f,  0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },

		// bottom face
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 0.0f },
	};

	constexpr int vBufferSize = sizeof(vList);

	// create default heap
	// default heap is memory on the GPU. Only the GPU has access to this memory
	// To get data into this heap, we will have to upload the data using
	// an upload heap
	{
		constexpr D3D12_HEAP_PROPERTIES heapProperties = {
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};
		constexpr D3D12_RESOURCE_DESC resourceDescription{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = (UINT64)vBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties, // a default heap
			D3D12_HEAP_FLAG_NONE, // no flags
			&resourceDescription, // resource description for a buffer
			D3D12_RESOURCE_STATE_COPY_DEST, // we will start this heap in the copy destination state since we will copy data from the upload heap to this heap
			nullptr, // optimized clear value must be null for this type of resource. used for render targets and depth/stencil buffers
			IID_PPV_ARGS(&vertexBuffer)));
	}
	// we can give resource heaps a name so when we debug with the graphics debugger we know what resource we are looking at
	vertexBuffer->SetName(L"Vertex Buffer Resource Heap");

	// create upload heap
	// upload heaps are used to upload data to the GPU. CPU can write to it, GPU can read from it
	// We will upload the vertex buffer using this heap to the default heap
	ComPtr<ID3D12Resource> vBufferUploadHeap;
	{
		constexpr D3D12_HEAP_PROPERTIES heapProperties = {
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};
		constexpr D3D12_RESOURCE_DESC ResourceDescription =
		{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = vBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties, // upload heap
			D3D12_HEAP_FLAG_NONE, // no flags
			&ResourceDescription, // resource description for a buffer
			D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read from this buffer and copy its contents to the default heap
			nullptr,
			IID_PPV_ARGS(&vBufferUploadHeap)));
	}
	vBufferUploadHeap->SetName(L"Vertex Buffer Upload Resource Heap");

	// store vertex buffer in upload heap
	const D3D12_SUBRESOURCE_DATA vertexData = {
		.pData = reinterpret_cast<BYTE*>(vList), // pointer to our vertex array
		.RowPitch = vBufferSize, // size of all our triangle vertex data
		.SlicePitch = vBufferSize // also the size of our triangle vertex data
	};
	// we are now creating a command with the command list to copy the data from
	// the upload heap to the default heap
	{
		const auto pCmdList = commandList.Get();
		const auto pDestinationResource = vertexBuffer.Get();
		const auto pIntermediate = vBufferUploadHeap.Get();
		constexpr auto IntermediateOffset = 0;
		constexpr auto FirstSubresource = 0;
		constexpr auto NumSubresources = 1;
		const auto pSrcData = &vertexData;

		UINT64 RequiredSize = 0;
		UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
		if (MemToAlloc > SIZE_MAX)
		{
			return 0;
		}
		void* pMem = malloc(static_cast<SIZE_T>(MemToAlloc));
		if (pMem == nullptr)
		{
			return 0;
		}
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = static_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
		UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
		UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

		D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
		device->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);

		{
			// Minor validation
			D3D12_RESOURCE_DESC IntermediateDesc = pIntermediate->GetDesc();
			D3D12_RESOURCE_DESC DestinationDesc = pDestinationResource->GetDesc();
			if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
				IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
				RequiredSize > SIZE_T(-1) ||
				(DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					(FirstSubresource != 0 || NumSubresources != 1)))
			{
				return 0;
			}

			BYTE* pData;
			THROW_ON_FAIL(pIntermediate->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

			for (UINT i = 0; i < NumSubresources; ++i)
			{
				if (pRowSizesInBytes[i] > SIZE_T(-1)) return 0;
				D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, SIZE_T(pLayouts[i].Footprint.RowPitch) * SIZE_T(pNumRows[i]) };

				{
					for (UINT z = 0; z < pLayouts[i].Footprint.Depth; ++z)
					{
						BYTE* pDestSlice = static_cast<BYTE*>(DestData.pData) + DestData.SlicePitch * z;
						const BYTE* pSrcSlice = static_cast<const BYTE*>(pSrcData[i].pData) + pSrcData[i].SlicePitch * LONG_PTR(z);
						for (UINT y = 0; y < pNumRows[i]; ++y)
						{
							memcpy(pDestSlice + DestData.RowPitch * y,
								pSrcSlice + pSrcData[i].RowPitch * LONG_PTR(y),
								static_cast<SIZE_T>(pRowSizesInBytes[i]));
						}
					}
				}
			}
			pIntermediate->Unmap(0, nullptr);

			if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				pCmdList->CopyBufferRegion(pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
			}
			else
			{
				for (UINT i = 0; i < NumSubresources; ++i)
				{
					const D3D12_TEXTURE_COPY_LOCATION Dst =
					{
						.pResource = pDestinationResource,
						.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
						.PlacedFootprint = {
							.Offset = i + FirstSubresource,
							.Footprint = {}
						}
					};
					const D3D12_TEXTURE_COPY_LOCATION Src =
					{
						.pResource = pIntermediate,
						.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
						.PlacedFootprint = pLayouts[i]
					};
					pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
				}
			}
		}
		free(pMem);
	}
	// Create index buffer

	// a quad (2 triangles)
	DWORD iList[] = {
		// front face
		0, 1, 2, // first triangle
		0, 3, 1, // second triangle

		// left face
		4, 5, 6, // first triangle
		4, 7, 5, // second triangle

		// right face
		8, 9, 10, // first triangle
		8, 11, 9, // second triangle

		// back face
		12, 13, 14, // first triangle
		12, 15, 13, // second triangle

		// top face
		16, 17, 18, // first triangle
		16, 19, 17, // second triangle

		// bottom face
		20, 21, 22, // first triangle
		20, 23, 21, // second triangle
	};

	constexpr int iBufferSize = sizeof(iList);

	numCubeIndices = sizeof(iList) / sizeof(DWORD);
	{
		constexpr D3D12_HEAP_PROPERTIES heapProperties =
		{
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};
		constexpr D3D12_RESOURCE_DESC resourceDescription =
		{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = iBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};
		// create default heap to hold index buffer
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties, // a default heap
			D3D12_HEAP_FLAG_NONE, // no flags
			&resourceDescription,
			D3D12_RESOURCE_STATE_COPY_DEST, // start in the copy destination state
			nullptr, // optimized clear value must be null for this type of resource
			IID_PPV_ARGS(&indexBuffer)));
	}
	// we can give resource heaps a name so when we debug with the graphics debugger we know what resource we are looking at
	vertexBuffer->SetName(L"Index Buffer Resource Heap");

	// create upload heap to upload index buffer
	ComPtr<ID3D12Resource> iBufferUploadHeap;
	{
		constexpr D3D12_HEAP_PROPERTIES heapProperties{
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};
		constexpr D3D12_RESOURCE_DESC resourceDescription{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = vBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties, // upload heap
			D3D12_HEAP_FLAG_NONE, // no flags
			&resourceDescription,// resource description for a buffer
			D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read from this buffer and copy its contents to the default heap
			nullptr,
			IID_PPV_ARGS(&iBufferUploadHeap)));
	}
	vBufferUploadHeap->SetName(L"Index Buffer Upload Resource Heap");

	// store vertex buffer in upload heap
	const D3D12_SUBRESOURCE_DATA indexData =
	{
		.pData = reinterpret_cast<BYTE*>(iList), // pointer to our index array
		.RowPitch = iBufferSize, // size of all our index buffer
		.SlicePitch = iBufferSize // also the size of our index buffer
	};
	// we are now creating a command with the command list to copy the data from
	// the upload heap to the default heap
	{
		const auto pCmdList = commandList.Get();
		const auto pDestinationResource = indexBuffer.Get();
		const auto pIntermediate = iBufferUploadHeap.Get();
		constexpr auto IntermediateOffset = 0;
		constexpr auto FirstSubresource = 0;
		constexpr auto NumSubresources = 1;
		const auto pSrcData = &indexData;

		UINT64 RequiredSize = 0;
		UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
		if (MemToAlloc > SIZE_MAX)
		{
			return 0;
		}
		void* pMem = malloc(static_cast<SIZE_T>(MemToAlloc));
		if (pMem == nullptr)
		{
			return 0;
		}
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = static_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
		UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
		UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

		D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
		device->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);

		{
			// Minor validation
			D3D12_RESOURCE_DESC IntermediateDesc = pIntermediate->GetDesc();
			D3D12_RESOURCE_DESC DestinationDesc = pDestinationResource->GetDesc();
			if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
				IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
				RequiredSize > SIZE_T(-1) ||
				(DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					(FirstSubresource != 0 || NumSubresources != 1)))
			{
				return 0;
			}

			BYTE* pData;
			THROW_ON_FAIL(pIntermediate->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

			for (UINT i = 0; i < NumSubresources; ++i)
			{
				if (pRowSizesInBytes[i] > SIZE_T(-1)) return 0;
				D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, SIZE_T(pLayouts[i].Footprint.RowPitch) * SIZE_T(pNumRows[i]) };

				{
					for (UINT z = 0; z < pLayouts[i].Footprint.Depth; ++z)
					{
						BYTE* pDestSlice = static_cast<BYTE*>(DestData.pData) + DestData.SlicePitch * z;
						const BYTE* pSrcSlice = static_cast<const BYTE*>(pSrcData[i].pData) + pSrcData[i].SlicePitch * LONG_PTR(z);
						for (UINT y = 0; y < pNumRows[i]; ++y)
						{
							memcpy(pDestSlice + DestData.RowPitch * y,
								pSrcSlice + pSrcData[i].RowPitch * LONG_PTR(y),
								static_cast<SIZE_T>(pRowSizesInBytes[i]));
						}
					}
				}
			}
			pIntermediate->Unmap(0, nullptr);

			if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				pCmdList->CopyBufferRegion(pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
			}
			else
			{
				for (UINT i = 0; i < NumSubresources; ++i)
				{
					const D3D12_TEXTURE_COPY_LOCATION Dst =
					{
						.pResource = pDestinationResource,
						.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
						.PlacedFootprint = {
							.Offset = i + FirstSubresource,
							.Footprint = {}
						}
					};
					const D3D12_TEXTURE_COPY_LOCATION Src =
					{
						.pResource = pIntermediate,
						.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
						.PlacedFootprint = pLayouts[i]
					};
					pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
				}
			}
		}
		free(pMem);
	}
	{
		const D3D12_RESOURCE_BARRIER resourceBarrier =
		{
			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			.Transition = {
				.pResource = indexBuffer.Get(),
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
				.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
			}
		};
		// transition the vertex buffer data from copy destination state to vertex buffer state
		commandList->ResourceBarrier(1, &resourceBarrier);
	}
	// Create the depth/stencil buffer

	// create a depth stencil descriptor heap so we can get a pointer to the depth stencil buffer
	constexpr D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc =
	{
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		.NumDescriptors = 1,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
	};
	THROW_ON_FAIL(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsDescriptorHeap)));

	{
		constexpr D3D12_CLEAR_VALUE depthOptimizedClearValue =
		{
			.Format = DXGI_FORMAT_D32_FLOAT,
			.DepthStencil =
			{
				.Depth = 1.0f,
				.Stencil = 0
			}
		};
		constexpr D3D12_HEAP_PROPERTIES heapProperties =
		{
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};
		const D3D12_RESOURCE_DESC resourceDescription =
		{
			.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
			.Alignment = 0,
			.Width = (UINT64)Width,
			.Height = (UINT)Height,
			.DepthOrArraySize = 1,
			.MipLevels = 0,
			.Format = DXGI_FORMAT_D32_FLOAT,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
			.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		};
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&depthStencilBuffer)
		));
	}
	dsDescriptorHeap->SetName(L"Depth/Stencil Resource Heap");
	{
		constexpr D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {
			.Format = DXGI_FORMAT_D32_FLOAT,
			.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
			.Flags = D3D12_DSV_FLAG_NONE
		};
		device->CreateDepthStencilView(depthStencilBuffer.Get(), &depthStencilDesc, dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}
	// create the constant buffer resource heap
	// We will update the constant buffer one or more times per frame, so we will use only an upload heap
	// unlike previously we used an upload heap to upload the vertex and index data, and then copied over
	// to a default heap. If you plan to use a resource for more than a couple frames, it is usually more
	// efficient to copy to a default heap where it stays on the gpu. In this case, our constant buffer
	// will be modified and uploaded at least once per frame, so we only use an upload heap

	// first we will create a resource heap (upload heap) for each frame for the cubes constant buffers
	// As you can see, we are allocating 64KB for each resource we create. Buffer resource heaps must be
	// an alignment of 64KB. We are creating 3 resources, one for each frame. Each constant buffer is 
	// only a 4x4 matrix of floats in this tutorial. So with a float being 4 bytes, we have 
	// 16 floats in one constant buffer, and we will store 2 constant buffers in each
	// heap, one for each cube, thats only 64x2 bits, or 128 bits we are using for each
	// resource, and each resource must be at least 64KB (65536 bits)
	for (int i = 0; i < frameBufferCount; i++)
	{
		// create resource for cube 1
		{
			constexpr D3D12_HEAP_PROPERTIES heapProperties =
			{
				.Type = D3D12_HEAP_TYPE_UPLOAD,
				.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
				.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
				.CreationNodeMask = 1,
				.VisibleNodeMask = 1
			};
			constexpr D3D12_RESOURCE_DESC resourceDescription =
			{
				.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
				.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
				.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
				.Height = 1,
				.DepthOrArraySize = 1,
				.MipLevels = 1,
				.Format = DXGI_FORMAT_UNKNOWN,
				.SampleDesc = {
					.Count = 1,
					.Quality = 0
				},
				.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
				.Flags = D3D12_RESOURCE_FLAG_NONE
			};
			THROW_ON_FAIL(device->CreateCommittedResource(
				&heapProperties, // this heap will be used to upload the constant buffer data
				D3D12_HEAP_FLAG_NONE, // no flags
				&resourceDescription,
				// size of the resource heap. Must be a multiple of 64KB for single-textures and constant buffers
				D3D12_RESOURCE_STATE_GENERIC_READ, // will be data that is read from so we keep it in the generic read state
				nullptr, // we do not have use an optimized clear value for constant buffers
				__uuidof(ID3D12Resource),
				&constantBufferUploadHeaps[i]
			));
		}
		constantBufferUploadHeaps[i]->SetName(L"Constant Buffer Upload Resource Heap");

		memset(&cbPerObject, 0, sizeof(cbPerObject));

		D3D12_RANGE readRange =
		{
			.Begin = 0,
			.End = 0
		};    // We do not intend to read from this resource on the CPU. (so end is less than or equal to begin)

		// map the resource heap to get a gpu virtual address to the beginning of the heap
		constantBufferUploadHeaps[i]->Map(0, &readRange, reinterpret_cast<void**>(&cbvGPUAddress[i]));

		// Because of the constant read alignment requirements, constant buffer views must be 256 bit aligned. Our buffers are smaller than 256 bits,
		// so we need to add spacing between the two buffers, so that the second buffer starts at 256 bits from the beginning of the resource heap.
		memcpy(cbvGPUAddress[i], &cbPerObject, sizeof(cbPerObject)); // cube1's constant buffer data
		memcpy(cbvGPUAddress[i] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject)); // cube2's constant buffer data
	}

	// load the image, create a texture resource and descriptor heap

	// Load the image from file

	D3D12_RESOURCE_DESC textureDesc;
	int imageBytesPerRow;
	BYTE* imageData;
	int imageSize;


	imageSize = LoadImageDataFromFile(&imageData, textureDesc, L"download.jpg", imageBytesPerRow);

	// make sure we have data
	if (imageSize <= 0)
	{
		Running = false;
		return false;
	}


	{
		constexpr D3D12_HEAP_PROPERTIES heapProperties =
		{
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};

		// create a default heap where the upload heap will copy its contents into (contents being the texture)
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties, // a default heap
			D3D12_HEAP_FLAG_NONE, // no flags
			&textureDesc, // the description of our texture
			D3D12_RESOURCE_STATE_COPY_DEST, // We will copy the texture from the upload heap to here, so we start it out in a copy dest state
			nullptr, // used for render targets and depth/stencil buffers
			IID_PPV_ARGS(&textureBuffer)));
	}
	textureBuffer->SetName(L"Texture Buffer Resource Heap");
	UINT64 textureUploadBufferSize;
	// this function gets the size an upload buffer needs to be to upload a texture to the gpu.
	// each row must be 256 byte aligned except for the last row, which can just be the size in bytes of the row
	// eg. textureUploadBufferSize = ((((width * numBytesPerPixel) + 255) & ~255) * (height - 1)) + (width * numBytesPerPixel);
	//textureUploadBufferSize = (((imageBytesPerRow + 255) & ~255) * (textureDesc.Height - 1)) + imageBytesPerRow;
	device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);
	{
		constexpr D3D12_HEAP_PROPERTIES heapProperties =
		{
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};
		const D3D12_RESOURCE_DESC resourceDesc =
		{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = textureUploadBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};
		// now we create an upload heap to upload our texture to the GPU
		THROW_ON_FAIL(device->CreateCommittedResource(
			&heapProperties, // upload heap
			D3D12_HEAP_FLAG_NONE, // no flags
			&resourceDesc, // resource description for a buffer (storing the image data in this heap just to copy to the default heap)
			D3D12_RESOURCE_STATE_GENERIC_READ, // We will copy the contents from this heap to the default heap above
			nullptr,
			IID_PPV_ARGS(&textureBufferUploadHeap)));
	}
	textureBufferUploadHeap->SetName(L"Texture Buffer Upload Resource Heap");

	// store vertex buffer in upload heap
	const D3D12_SUBRESOURCE_DATA textureData = {
		.pData = &imageData[0], // pointer to our image data
		.RowPitch = imageBytesPerRow, // size of all our triangle vertex data
		.SlicePitch = imageBytesPerRow * textureDesc.Height // also the size of our triangle vertex data
	};
	// Now we copy the upload buffer contents to the default heap
	{
		const auto pCmdList = commandList.Get();
		const auto pDestinationResource = textureBuffer.Get();
		const auto pIntermediate = textureBufferUploadHeap.Get();
		constexpr auto IntermediateOffset = 0;
		constexpr auto FirstSubresource = 0;
		constexpr auto NumSubresources = 1;
		const auto pSrcData = &textureData;

		UINT64 RequiredSize = 0;
		UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
		if (MemToAlloc > SIZE_MAX)
		{
			return 0;
		}
		void* pMem = malloc(static_cast<SIZE_T>(MemToAlloc));
		if (pMem == nullptr)
		{
			return 0;
		}
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = static_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
		UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
		UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

		D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
		device->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);

		{
			// Minor validation
			D3D12_RESOURCE_DESC IntermediateDesc = pIntermediate->GetDesc();
			D3D12_RESOURCE_DESC DestinationDesc = pDestinationResource->GetDesc();
			if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
				IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
				RequiredSize > SIZE_T(-1) ||
				(DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					(FirstSubresource != 0 || NumSubresources != 1)))
			{
				return 0;
			}

			BYTE* pData;
			THROW_ON_FAIL(pIntermediate->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

			for (UINT i = 0; i < NumSubresources; ++i)
			{
				if (pRowSizesInBytes[i] > SIZE_T(-1)) return 0;
				D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, SIZE_T(pLayouts[i].Footprint.RowPitch) * SIZE_T(pNumRows[i]) };

				{
					for (UINT z = 0; z < pLayouts[i].Footprint.Depth; ++z)
					{
						BYTE* pDestSlice = static_cast<BYTE*>(DestData.pData) + DestData.SlicePitch * z;
						const BYTE* pSrcSlice = static_cast<const BYTE*>(pSrcData[i].pData) + pSrcData[i].SlicePitch * LONG_PTR(z);
						for (UINT y = 0; y < pNumRows[i]; ++y)
						{
							memcpy(pDestSlice + DestData.RowPitch * y,
								pSrcSlice + pSrcData[i].RowPitch * LONG_PTR(y),
								static_cast<SIZE_T>(pRowSizesInBytes[i]));
						}
					}
				}
			}
			pIntermediate->Unmap(0, nullptr);

			if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				pCmdList->CopyBufferRegion(pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
			}
			else
			{
				for (UINT i = 0; i < NumSubresources; ++i)
				{
					D3D12_TEXTURE_COPY_LOCATION Dst =
					{
						.pResource = pDestinationResource,
						.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
						.PlacedFootprint = {
							.Offset = i + FirstSubresource,
							.Footprint = {}
						}
					};
					D3D12_TEXTURE_COPY_LOCATION Src{
						.pResource = pIntermediate,
						.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
						.PlacedFootprint = pLayouts[i]
					};
					pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
				}
			}
		}
		free(pMem);
	}
	// transition the texture default heap to a pixel shader resource (we will be sampling from this heap in the pixel shader to get the color of pixels)
	{
		const D3D12_RESOURCE_BARRIER resourceBarrier = {
			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			.Transition = {
				.pResource = textureBuffer.Get(),
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
				.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			}
		};
		commandList->ResourceBarrier(1, &resourceBarrier);
	}
	{
		// create the descriptor heap that will store our srv
		constexpr D3D12_DESCRIPTOR_HEAP_DESC heapDesc =
		{
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			.NumDescriptors = 1,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		};
		THROW_ON_FAIL(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mainDescriptorHeap)));
	}
	{
		// now we create a shader resource view (descriptor that points to the texture and describes it)
		const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = textureDesc.Format,
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D = {.MipLevels = 1 }
		};

		device->CreateShaderResourceView(textureBuffer.Get(), &srvDesc, mainDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}
	// Now we execute the command list to upload the initial assets (triangle data)
	commandList->Close();
	ComPtr<ID3D12CommandList> ppCommandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists->GetAddressOf());

	// increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
	fenceValue[frameIndex]++;
	THROW_ON_FAIL(commandQueue->Signal(fence[frameIndex].Get(), fenceValue[frameIndex]));

	// we are done with image data now that we've uploaded it to the gpu, so free it up
	free(imageData);

	// create a vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
	{
		.BufferLocation = vertexBuffer->GetGPUVirtualAddress(),
		.SizeInBytes = (UINT)vBufferSize,
		.StrideInBytes = sizeof(Vertex)
	};
	// create a vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	const D3D12_INDEX_BUFFER_VIEW indexBufferView =
	{
		.BufferLocation = indexBuffer->GetGPUVirtualAddress(),
		.SizeInBytes = (UINT)iBufferSize,
		.Format = DXGI_FORMAT_R32_UINT // 32-bit unsigned integer (this is what a dword is, double word, a word is 2 bytes)

	};

	// Fill out the Viewport
	const D3D12_VIEWPORT viewport =
	{
		.TopLeftX = 0,
		.TopLeftY = 0,
		.Width = static_cast<float>(Width),
		.Height = static_cast<float>(Height),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f
	};

	// Fill out a scissor rect
	const D3D12_RECT scissorRect = {
		.left = 0,
		.top = 0,
		.right = Width,
		.bottom = Height
	};
	// build projection and view matrix
	DirectX::XMMATRIX tmpMat = DirectX::XMMatrixPerspectiveFovLH(45.0f * (3.14f / 180.0f), (float)Width / (float)Height, 0.1f, 1000.0f);
	XMStoreFloat4x4(&cameraProjMat, tmpMat);

	// set starting camera state
	cameraPosition = DirectX::XMFLOAT4(0.0f, 2.0f, -4.0f, 0.0f);
	cameraTarget = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	cameraUp = DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f);

	// build view matrix
	DirectX::XMVECTOR cPos = XMLoadFloat4(&cameraPosition);
	DirectX::XMVECTOR cTarg = XMLoadFloat4(&cameraTarget);
	DirectX::XMVECTOR cUp = XMLoadFloat4(&cameraUp);
	tmpMat = DirectX::XMMatrixLookAtLH(cPos, cTarg, cUp);
	XMStoreFloat4x4(&cameraViewMat, tmpMat);

	// set starting cubes position
	// first cube
	cube1Position = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f); // set cube 1's position
	DirectX::XMVECTOR posVec = XMLoadFloat4(&cube1Position); // create xmvector for cube1's position

	tmpMat = DirectX::XMMatrixTranslationFromVector(posVec); // create translation matrix from cube1's position vector
	XMStoreFloat4x4(&cube1RotMat, DirectX::XMMatrixIdentity()); // initialize cube1's rotation matrix to identity matrix
	XMStoreFloat4x4(&cube1WorldMat, tmpMat); // store cube1's world matrix

	// second cube
	cube2PositionOffset = DirectX::XMFLOAT4(1.5f, 0.0f, 0.0f, 0.0f);
	posVec = XMLoadFloat4(&cube2PositionOffset);// +XMLoadFloat4(&cube1Position); // create xmvector for cube2's position
																				// we are rotating around cube1 here, so add cube2's position to cube1

	tmpMat = DirectX::XMMatrixTranslationFromVector(posVec); // create translation matrix from cube2's position offset vector
	XMStoreFloat4x4(&cube2RotMat, DirectX::XMMatrixIdentity()); // initialize cube2's rotation matrix to identity matrix
	XMStoreFloat4x4(&cube2WorldMat, tmpMat); // store cube2's world matrix

	// start the main loop
	{
		MSG msg;
		memset(&msg, 0, sizeof(MSG));

		while (Running)
		{
			if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
					break;

				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
			else {
				// run game code
				// update the game logic
				// update app logic, such as moving the camera or figuring out what objects are in view

				// create rotation matrices
				DirectX::XMMATRIX rotXMat = DirectX::XMMatrixRotationX(0.0001f);
				DirectX::XMMATRIX rotYMat = DirectX::XMMatrixRotationY(0.0002f);
				DirectX::XMMATRIX rotZMat = DirectX::XMMatrixRotationZ(0.0003f);

				// add rotation to cube1's rotation matrix and store it
				DirectX::XMMATRIX rotMat = XMLoadFloat4x4(&cube1RotMat) * rotXMat * rotYMat * rotZMat;
				XMStoreFloat4x4(&cube1RotMat, rotMat);

				// create translation matrix for cube 1 from cube 1's position vector
				DirectX::XMMATRIX translationMat = DirectX::XMMatrixTranslationFromVector(XMLoadFloat4(&cube1Position));

				// create cube1's world matrix by first rotating the cube, then positioning the rotated cube
				DirectX::XMMATRIX worldMat = rotMat * translationMat;

				// store cube1's world matrix
				XMStoreFloat4x4(&cube1WorldMat, worldMat);

				// update constant buffer for cube1
				// create the wvp matrix and store in constant buffer
				DirectX::XMMATRIX viewMat = XMLoadFloat4x4(&cameraViewMat); // load view matrix
				DirectX::XMMATRIX projMat = XMLoadFloat4x4(&cameraProjMat); // load projection matrix
				DirectX::XMMATRIX wvpMat = XMLoadFloat4x4(&cube1WorldMat) * viewMat * projMat; // create wvp matrix
				DirectX::XMMATRIX transposed = DirectX::XMMatrixTranspose(wvpMat); // must transpose wvp matrix for the gpu
				XMStoreFloat4x4(&cbPerObject.wvpMat, transposed); // store transposed wvp matrix in constant buffer

				// copy our ConstantBuffer instance to the mapped constant buffer resource
				memcpy(cbvGPUAddress[frameIndex], &cbPerObject, sizeof(cbPerObject));

				// now do cube2's world matrix
				// create rotation matrices for cube2
				rotXMat = DirectX::XMMatrixRotationX(0.0003f);
				rotYMat = DirectX::XMMatrixRotationY(0.0002f);
				rotZMat = DirectX::XMMatrixRotationZ(0.0001f);

				// add rotation to cube2's rotation matrix and store it
				rotMat = rotZMat * (XMLoadFloat4x4(&cube2RotMat) * (rotXMat * rotYMat));
				XMStoreFloat4x4(&cube2RotMat, rotMat);

				// create translation matrix for cube 2 to offset it from cube 1 (its position relative to cube1
				DirectX::XMMATRIX translationOffsetMat = DirectX::XMMatrixTranslationFromVector(XMLoadFloat4(&cube2PositionOffset));

				// we want cube 2 to be half the size of cube 1, so we scale it by .5 in all dimensions
				DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScaling(0.5f, 0.5f, 0.5f);

				// reuse worldMat. 
				// first we scale cube2. scaling happens relative to point 0,0,0, so you will almost always want to scale first
				// then we translate it. 
				// then we rotate it. rotation always rotates around point 0,0,0
				// finally we move it to cube 1's position, which will cause it to rotate around cube 1
				worldMat = scaleMat * translationOffsetMat * rotMat * translationMat;

				wvpMat = XMLoadFloat4x4(&cube2WorldMat) * viewMat * projMat; // create wvp matrix
				transposed = XMMatrixTranspose(wvpMat); // must transpose wvp matrix for the gpu
				XMStoreFloat4x4(&cbPerObject.wvpMat, transposed); // store transposed wvp matrix in constant buffer

				// copy our ConstantBuffer instance to the mapped constant buffer resource
				memcpy(cbvGPUAddress[frameIndex] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject));

				// store cube2's world matrix
				XMStoreFloat4x4(&cube2WorldMat, worldMat);
				// execute the command queue (rendering the scene is the result of the gpu executing the command lists)
				 // update the pipeline by sending commands to the commandqueue

				{

					// We have to wait for the gpu to finish with the command allocator before we reset it
					WaitForPreviousFrame();

					// we can only reset an allocator once the gpu is done with it
					// resetting an allocator frees the memory that the command list was stored in
					THROW_ON_FAIL(commandAllocator[frameIndex]->Reset());

					// reset the command list. by resetting the command list we are putting it into
					// a recording state so we can start recording commands into the command allocator.
					// the command allocator that we reference here may have multiple command lists
					// associated with it, but only one can be recording at any time. Make sure
					// that any other command lists associated to this command allocator are in
					// the closed state (not recording).
					// Here you will pass an initial pipeline state object as the second parameter,
					// but in this tutorial we are only clearing the rtv, and do not actually need
					// anything but an initial default pipeline, which is what we get by setting
					// the second parameter to NULL
					THROW_ON_FAIL(commandList->Reset(commandAllocator[frameIndex].Get(), pipelineStateObject.Get()));

					// here we start recording commands into the commandList (which all the commands will be stored in the commandAllocator)

					// transition the "frameIndex" render target from the present state to the render target state so the command list draws to it starting from here
					{
						const D3D12_RESOURCE_BARRIER resourceBarrier =
						{
							.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
							.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
							.Transition = {
								.pResource = renderTargets[frameIndex].Get(),
								.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
								.StateBefore = D3D12_RESOURCE_STATE_PRESENT,
								.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
							}
						};
						commandList->ResourceBarrier(1, &resourceBarrier);
					}
					// here we again get the handle to our current render target view so we can set it as the render target in the output merger stage of the pipeline
					D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
					{
						.ptr = SIZE_T(INT64(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr) + INT64(frameIndex) * INT64(rtvDescriptorSize))
					};

					// get a handle to the depth/stencil buffer
					D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

					// set the render target for the output merger stage (the output of the pipeline)
					commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

					// Clear the render target by using the ClearRenderTargetView command
					const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
					commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

					// clear the depth/stencil buffer
					commandList->ClearDepthStencilView(dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

					// set root signature
					commandList->SetGraphicsRootSignature(rootSignature.Get()); // set the root signature

					// set the descriptor heap
					ComPtr<ID3D12DescriptorHeap> descriptorHeaps[] = { mainDescriptorHeap.Get() };
					commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps->GetAddressOf());

					// set the descriptor table to the descriptor heap (parameter 1, as constant buffer root descriptor is parameter index 0)
					commandList->SetGraphicsRootDescriptorTable(1, mainDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

					commandList->RSSetViewports(1, &viewport); // set the viewports
					commandList->RSSetScissorRects(1, &scissorRect); // set the scissor rects
					commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // set the primitive topology
					commandList->IASetVertexBuffers(0, 1, &vertexBufferView); // set the vertex buffer (using the vertex buffer view)
					commandList->IASetIndexBuffer(&indexBufferView);

					// first cube

					// set cube1's constant buffer
					commandList->SetGraphicsRootConstantBufferView(0, constantBufferUploadHeaps[frameIndex]->GetGPUVirtualAddress());

					// draw first cube
					commandList->DrawIndexedInstanced(numCubeIndices, 1, 0, 0, 0);

					// second cube

					// set cube2's constant buffer. You can see we are adding the size of ConstantBufferPerObject to the constant buffer
					// resource heaps address. This is because cube1's constant buffer is stored at the beginning of the resource heap, while
					// cube2's constant buffer data is stored after (256 bits from the start of the heap).
					commandList->SetGraphicsRootConstantBufferView(0, constantBufferUploadHeaps[frameIndex]->GetGPUVirtualAddress() + ConstantBufferPerObjectAlignedSize);

					// draw second cube
					commandList->DrawIndexedInstanced(numCubeIndices, 1, 0, 0, 0);
					{
						const D3D12_RESOURCE_BARRIER resourceBarrier =
						{
							.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
							.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
							.Transition = {
								.pResource = renderTargets[frameIndex].Get(),
								.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
								.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
								.StateAfter = D3D12_RESOURCE_STATE_PRESENT
							}
						};

						// transition the "frameIndex" render target from the render target state to the present state. If the debug layer is enabled, you will receive a
						// warning if present is called on the render target when it's not in the present state
						commandList->ResourceBarrier(1, &resourceBarrier);
					}
					THROW_ON_FAIL(commandList->Close());
				}

				// create an array of command lists (only one command list here)
				ComPtr<ID3D12CommandList> ppCommandLists[] = { commandList.Get() };

				// execute the array of command lists
				commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists->GetAddressOf());

				// this command goes in at the end of our command queue. we will know when our command queue 
				// has finished because the fence value will be set to "fenceValue" from the GPU since the command
				// queue is being executed on the GPU
				THROW_ON_FAIL(commandQueue->Signal(fence[frameIndex].Get(), fenceValue[frameIndex]));

				// present the current backbuffer
				THROW_ON_FAIL(swapChain->Present(0, 0));
			}
		}
	}

	// we want to wait for the gpu to finish executing the command list before we start releasing everything
	WaitForPreviousFrame();

	// close the fence event
	CloseHandle(fenceEvent);

	// clean up everything

	// wait for the gpu to finish all frames
	for (int i = 0; i < frameBufferCount; ++i)
	{
		frameIndex = i;
		WaitForPreviousFrame();
	}

	// get swapchain out of full screen before exiting
	BOOL fs = false;
	if (swapChain->GetFullscreenState(&fs, nullptr) != NULL)
		swapChain->SetFullscreenState(false, nullptr);

	return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)

{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			if (MessageBox(0, L"Are you sure you want to exit?",
				L"Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				Running = false;
				DestroyWindow(hwnd);
			}
		}
		return 0;

	case WM_DESTROY: // x button on top right corner of window was pressed
		Running = false;
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd,
		msg,
		wParam,
		lParam);
}

// get the dxgi format equivilent of a wic format
DXGI_FORMAT GetDXGIFormatFromWICFormat(WICPixelFormatGUID& wicFormatGUID)
{
	if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFloat) return DXGI_FORMAT_R32G32B32A32_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAHalf) return DXGI_FORMAT_R16G16B16A16_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBA) return DXGI_FORMAT_R16G16B16A16_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA) return DXGI_FORMAT_R8G8B8A8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGRA) return DXGI_FORMAT_B8G8R8A8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR) return DXGI_FORMAT_B8G8R8X8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102XR) return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;

	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102) return DXGI_FORMAT_R10G10B10A2_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGRA5551) return DXGI_FORMAT_B5G5R5A1_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR565) return DXGI_FORMAT_B5G6R5_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFloat) return DXGI_FORMAT_R32_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayHalf) return DXGI_FORMAT_R16_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGray) return DXGI_FORMAT_R16_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppGray) return DXGI_FORMAT_R8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppAlpha) return DXGI_FORMAT_A8_UNORM;

	else return DXGI_FORMAT_UNKNOWN;
}

// get a dxgi compatible wic format from another wic format
WICPixelFormatGUID GetConvertToWICFormat(WICPixelFormatGUID& wicFormatGUID)
{
	if (wicFormatGUID == GUID_WICPixelFormatBlackWhite) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat1bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat2bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat4bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat2bppGray) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat4bppGray) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayFixedPoint) return GUID_WICPixelFormat16bppGrayHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFixedPoint) return GUID_WICPixelFormat32bppGrayFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR555) return GUID_WICPixelFormat16bppBGRA5551;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR101010) return GUID_WICPixelFormat32bppRGBA1010102;
	else if (wicFormatGUID == GUID_WICPixelFormat24bppBGR) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat24bppRGB) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppPBGRA) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppPRGBA) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGB) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppBGR) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPBGRA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppBGRFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRAFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBHalf) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBHalf) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppPRGBAFloat) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFloat) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFixedPoint) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFixedPoint) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBE) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppCMYK) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppCMYK) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat40bppCMYKAlpha) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat80bppCMYKAlpha) return GUID_WICPixelFormat64bppRGBA;

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8) || defined(_WIN7_PLATFORM_UPDATE)
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGB) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGB) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBAHalf) return GUID_WICPixelFormat64bppRGBAHalf;
#endif

	else return GUID_WICPixelFormatDontCare;
}

// get the number of bits per pixel for a dxgi format
int GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat)
{
	if (dxgiFormat == DXGI_FORMAT_R32G32B32A32_FLOAT) return 128;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_UNORM) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R8G8B8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8X8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) return 32;

	else if (dxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B5G5R5A1_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_B5G6R5_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R32_FLOAT) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R16_FLOAT) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R16_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R8_UNORM) return 8;
	else if (dxgiFormat == DXGI_FORMAT_A8_UNORM) return 8;
}

// load and decode image from file
int LoadImageDataFromFile(BYTE** imageData, D3D12_RESOURCE_DESC& resourceDescription, const LPCWSTR&& filename, int& bytesPerRow)
{

	// we only need one instance of the imaging factory to create decoders and frames
	static ComPtr<IWICImagingFactory> wicFactory;

	// reset decoder, frame and converter since these will be different for each image we load
	ComPtr<IWICBitmapDecoder> wicDecoder;
	ComPtr<IWICBitmapFrameDecode> wicFrame;
	ComPtr<IWICFormatConverter> wicConverter;

	bool imageConverted = false;

	if (wicFactory == NULL)
	{
		// Initialize the COM library
		CoInitialize(nullptr);

		// create the WIC factory
		THROW_ON_FAIL(CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&wicFactory)
		));
	}

	// load a decoder for the image
	THROW_ON_FAIL(wicFactory->CreateDecoderFromFilename(
		filename,                        // Image we want to load in
		nullptr,                            // This is a vendor ID, we do not prefer a specific one so set to null
		GENERIC_READ,                    // We want to read from this file
		WICDecodeMetadataCacheOnLoad,    // We will cache the metadata right away, rather than when needed, which might be unknown
		&wicDecoder                      // the wic decoder to be created
	));

	// get image from decoder (this will decode the "frame")
	THROW_ON_FAIL(wicDecoder->GetFrame(0, &wicFrame));

	// get wic pixel format of image
	WICPixelFormatGUID pixelFormat;
	THROW_ON_FAIL(wicFrame->GetPixelFormat(&pixelFormat));

	// get size of image
	UINT textureWidth, textureHeight;
	THROW_ON_FAIL(wicFrame->GetSize(&textureWidth, &textureHeight));

	// we are not handling sRGB types in this tutorial, so if you need that support, you'll have to figure
	// out how to implement the support yourself

	// convert wic pixel format to dxgi pixel format
	DXGI_FORMAT dxgiFormat = GetDXGIFormatFromWICFormat(pixelFormat);

	// if the format of the image is not a supported dxgi format, try to convert it
	if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
	{
		// get a dxgi compatible wic format from the current image format
		WICPixelFormatGUID convertToPixelFormat = GetConvertToWICFormat(pixelFormat);

		// return if no dxgi compatible format was found
		if (convertToPixelFormat == GUID_WICPixelFormatDontCare) return 0;

		// set the dxgi format
		dxgiFormat = GetDXGIFormatFromWICFormat(convertToPixelFormat);

		// create the format converter
		THROW_ON_FAIL(wicFactory->CreateFormatConverter(&wicConverter));

		// make sure we can convert to the dxgi compatible format
		BOOL canConvert = false;
		THROW_ON_FAIL(wicConverter->CanConvert(pixelFormat, convertToPixelFormat, &canConvert));

		// do the conversion (wicConverter will contain the converted image)
		THROW_ON_FAIL(wicConverter->Initialize(wicFrame.Get(), convertToPixelFormat, WICBitmapDitherTypeErrorDiffusion, 0, 0, WICBitmapPaletteTypeCustom));

		// this is so we know to get the image data from the wicConverter (otherwise we will get from wicFrame)
		imageConverted = true;
	}

	int bitsPerPixel = GetDXGIFormatBitsPerPixel(dxgiFormat); // number of bits per pixel
	bytesPerRow = (textureWidth * bitsPerPixel) / 8; // number of bytes in each row of the image data
	int imageSize = bytesPerRow * textureHeight; // total image size in bytes

	// allocate enough memory for the raw image data, and set imageData to point to that memory
	*imageData = (BYTE*)malloc(imageSize);

	// copy (decoded) raw image data into the newly allocated memory (imageData)
	if (imageConverted)
	{
		// if image format needed to be converted, the wic converter will contain the converted image
		THROW_ON_FAIL(wicConverter->CopyPixels(0, bytesPerRow, imageSize, *imageData));
	}
	else
	{
		// no need to convert, just copy data from the wic frame
		THROW_ON_FAIL(wicFrame->CopyPixels(0, bytesPerRow, imageSize, *imageData));
	}

	// now describe the texture with the information we have obtained from the image
	resourceDescription = {
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Alignment = 0, // may be 0, 4KB, 64KB, or 4MB. 0 will let runtime decide between 64KB and 4MB (4MB for multi-sampled textures)
		.Width = textureWidth, // width of the texture
		.Height = textureHeight, // height of the texture
		.DepthOrArraySize = 1, // if 3d image, depth of 3d image. Otherwise an array of 1D or 2D textures (we only have one image, so we set 1)
		.MipLevels = 1, // Number of mipmaps. We are not generating mipmaps for this texture, so we have only one level
		.Format = dxgiFormat, // This is the dxgi format of the image (format of the pixels)
		.SampleDesc = {.Count = 1, .Quality = 0},
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN, // The arrangement of the pixels. Setting to unknown lets the driver choose the most efficient one
		.Flags = D3D12_RESOURCE_FLAG_NONE // no flags
	};
	std::cout << "width_: " << std::dec << textureWidth << std::endl;
	std::cout << "height: " << std::dec << textureHeight << std::endl;
	// return the size of the image. remember to delete the image once your done with it (in this tutorial once its uploaded to the gpu)
	return imageSize;
}
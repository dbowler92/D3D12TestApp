#include <windows.h>
#include <wrl.h> 

#include <dxgi1_4.h>
#include <d3d12.h>

#include <dxgidebug.h>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include "d3dx12.h"

#include "GameTimer.h"

//Link D3D12 dependencies 
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace DirectX;
using namespace Microsoft::WRL;

#define Assert(x) \
	if (!(x)) { MessageBoxA(0, #x, "Assert Failed", MB_OK); __debugbreak(); }

#define Check(x) \
	if (!(x)) { MessageBoxA(0, #x, "Check Failed", MB_OK); __debugbreak(); }

#define CheckHResult(HResult) \
	Check(SUCCEEDED(HResult))

#define RIID(x) \
	IID_PPV_ARGS(x)

//Global settings/data
const unsigned SwapchainBufferCount = 2;
const DXGI_FORMAT SwapchainBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
const DXGI_FORMAT DepthStencilBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
const bool bEnableMSAA = true; //TODO:
const unsigned MSAACount = 4;  //TODO:

//App data
bool gQuit = false;

//Windows window
HWND Window;

//D3D12 init
#if defined(DEBUG) || defined(_DEBUG)
ComPtr<IDXGIDebug1> DebugDXGIController;
ComPtr<ID3D12Debug1> DebugController;
#endif 

ComPtr<IDXGIFactory4> DXGIFactory;

ComPtr<ID3D12Device1> Device;
ComPtr<ID3D12Fence1> D3D12Fence;
ComPtr<ID3D12CommandQueue> DirectGraphicsCommandQueue;
ComPtr<ID3D12CommandAllocator> DirectGraphicsCommandListAllocator;
ComPtr<ID3D12GraphicsCommandList1> CommandList;
ComPtr<IDXGISwapChain1> Swapchain;
ComPtr<ID3D12Resource> SwapchainColourBuffers[SwapchainBufferCount];
ComPtr<ID3D12Resource> DepthStencilBufferResource;

//Descriptor heaps for swapchain resources (RTV's and DSV)
ComPtr<ID3D12DescriptorHeap> SwapchainRTVDescriptorHeap;
ComPtr<ID3D12DescriptorHeap> SwapchainDSVDescriptorHeap;

//Graphics data
UINT RTVDescriptorStride;
UINT DSVDescriptorStride;
UINT CBVDescriptorStride;

D3D12_VIEWPORT Viewport;

//Graphics runtime data
unsigned CurrentFenceValue = 0;
unsigned CurrentSwapchainColourBufferIdx = 0;

unsigned ScreenWidth = 0;
unsigned ScreenHeight = 0;

LRESULT CALLBACK WindowProc(HWND Window, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	LRESULT Result = 0;

	switch (Msg)
	{
		case WM_QUIT:
		case WM_DESTROY:
		{
			gQuit = true;
			break;
		}
		default:
		{
			Result = DefWindowProcA(Window, Msg, WParam, LParam);
			break;
		}
	}

	return Result;
}


bool InitWindow(HINSTANCE Instance, HINSTANCE PrevInstance,
	LPSTR CmdLine, int CmdShow)
{
	//Class
	WNDCLASS WindowClass = {};
	WindowClass.lpfnWndProc = WindowProc;
	WindowClass.lpszClassName = "D3D12 Test App";
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClass(&WindowClass);

	//Window.
	Window = CreateWindow("D3D12 Test App", "D3D12 Test", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, Instance, 0);

	ShowWindow(Window, SW_SHOW);

	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForSwapchainColourBuffer(INT idx)
{
	//TODO: Handle if MSAA enabled
	Assert(idx < SwapchainBufferCount); //TODO: Assert logic may change with MSAA enabled...

	//TODO: Remove d3dx12 code + do it ourselves
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(SwapchainRTVDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
		idx, RTVDescriptorStride);
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForDepthStencilBuffer()
{
	return SwapchainDSVDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart();
}


void FlushCommandQueue()
{
	//Increment the fence value we are waiting on...
	CurrentFenceValue++;

	//Add instruction to queue to set a new fence point after previous instructions
	CheckHResult(DirectGraphicsCommandQueue->Signal(D3D12Fence.Get(), CurrentFenceValue));

	//Wait until GPU has completed tasks up until this fence point...
	if (D3D12Fence.Get()->GetCompletedValue() < CurrentFenceValue)
	{
		HANDLE EventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		//Fire event when GPU hits the current fence
		CheckHResult(D3D12Fence.Get()->SetEventOnCompletion(CurrentFenceValue, EventHandle));

		WaitForSingleObject(EventHandle, INFINITE);
		CloseHandle(EventHandle);
	}
}

bool InitD3D12()
{
#if defined(DEBUG) || defined(_DEBUG)
	//Debug
	CheckHResult(D3D12GetDebugInterface(IID_PPV_ARGS(DebugController.GetAddressOf())));
	DebugController.Get()->EnableDebugLayer();
	
	CheckHResult(DXGIGetDebugInterface1(0, IID_PPV_ARGS(DebugDXGIController.GetAddressOf())));
	DebugDXGIController.Get()->EnableLeakTrackingForThread();

	CheckHResult(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(DXGIFactory.GetAddressOf())));
#else
	//Non debug DXGI factory.
	CheckHResult(CreateDXGIFactory2(0, IID_PPV_ARGS(DXGIFactory.GetAddressOf())));
#endif

	//Device
	CheckHResult(D3D12CreateDevice(0, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(Device.GetAddressOf())));

	//A fence
	CheckHResult(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(D3D12Fence.GetAddressOf())));

	//Cache Descriptor set sizes
	RTVDescriptorStride = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	DSVDescriptorStride = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	CBVDescriptorStride = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//Command queue
	D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = {};
	CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	CommandQueueDesc.NodeMask = 0;
	CheckHResult(Device->CreateCommandQueue(&CommandQueueDesc, IID_PPV_ARGS(DirectGraphicsCommandQueue.GetAddressOf())));

	//Command list allocator 
	CheckHResult(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(DirectGraphicsCommandListAllocator.GetAddressOf())));

	//Command list from above allocator
	CheckHResult(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		DirectGraphicsCommandListAllocator.Get(), nullptr, IID_PPV_ARGS(CommandList.GetAddressOf())));

	DXGI_SWAP_CHAIN_DESC1 SwapchainDesc = {};
	SwapchainDesc.BufferCount = SwapchainBufferCount;
	SwapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	SwapchainDesc.Format = SwapchainBufferFormat;
	SwapchainDesc.Width = 0;
	SwapchainDesc.Height = 0;
	//SwapchainDesc.Scaling = DXGI_SCALING_NONE;
	SwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapchainDesc.SampleDesc.Count = 1;	   //Need to handle MSAA ourselves by
	SwapchainDesc.SampleDesc.Quality = 0;  //Rendering in to an MSAA target and then downsampling via a custom shader...

	CheckHResult(DXGIFactory->CreateSwapChainForHwnd(DirectGraphicsCommandQueue.Get(), 
		Window, &SwapchainDesc, nullptr, nullptr, Swapchain.GetAddressOf()));

	//Get swapchain desc back out - gives us width and height
	CheckHResult(Swapchain.Get()->GetDesc1(&SwapchainDesc));
	ScreenWidth = SwapchainDesc.Width;
	ScreenHeight = SwapchainDesc.Height;
	
	//RTV descriptor heap - 1 RTV per swapchain colour buffer
	D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
	DescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DescriptorHeapDesc.NodeMask = 0;
	DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	DescriptorHeapDesc.NumDescriptors = SwapchainBufferCount; //TODO: Extra RTV(s) for MSAA...
	CheckHResult(Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(SwapchainRTVDescriptorHeap.GetAddressOf())));

	//DSV descriptor heap - 1 for the depth/stencil buffer we will be creating.
	DescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DescriptorHeapDesc.NodeMask = 0;
	DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	DescriptorHeapDesc.NumDescriptors = 1; //TODO: Extra RTV(s) for MSAA...
	CheckHResult(Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(SwapchainDSVDescriptorHeap.GetAddressOf())));

	//Create an RTV to each of the swapchain colour buffers
	for (int i = 0; i < SwapchainBufferCount; ++i)
	{
		//Get the resource
		CheckHResult(Swapchain->GetBuffer(i, IID_PPV_ARGS(SwapchainColourBuffers[i].GetAddressOf())));

		//Handle
		D3D12_CPU_DESCRIPTOR_HANDLE RTVCpuHandle = GetCPUDescriptorHandleForSwapchainColourBuffer(i);

		//Create RTV to the swapchain colour buffer
		Device->CreateRenderTargetView(SwapchainColourBuffers[i].Get(), nullptr, RTVCpuHandle);
	}

	//Create the depth/stencil buffer we are using alongside the swapchain colour buffers
	//
	//Resource desc
	D3D12_RESOURCE_DESC DepthStencilBufferResourceDesc = {};
	DepthStencilBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	DepthStencilBufferResourceDesc.Alignment = 0;
	DepthStencilBufferResourceDesc.Width = ScreenWidth;
	DepthStencilBufferResourceDesc.Height = ScreenHeight;
	DepthStencilBufferResourceDesc.DepthOrArraySize = 1;
	DepthStencilBufferResourceDesc.MipLevels = 1;
	DepthStencilBufferResourceDesc.Format = DepthStencilBufferFormat;
	DepthStencilBufferResourceDesc.SampleDesc.Count = 1;	//TODO: MSAA
	DepthStencilBufferResourceDesc.SampleDesc.Quality = 0; //TODO: MSAA
	DepthStencilBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	DepthStencilBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	//Heap desc
	D3D12_CLEAR_VALUE DepthStencilClear = {};
	DepthStencilClear.Format = DepthStencilBufferFormat;
	DepthStencilClear.DepthStencil.Depth = 1.0f;
	DepthStencilClear.DepthStencil.Stencil = 0;

	//TODO: Remove need for d3dx12
	D3D12_HEAP_PROPERTIES DepthStencilHeapPropsDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	//Create the resource
	CheckHResult(Device->CreateCommittedResource(&DepthStencilHeapPropsDesc,
		D3D12_HEAP_FLAG_NONE,
		&DepthStencilBufferResourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&DepthStencilClear,
		IID_PPV_ARGS(DepthStencilBufferResource.GetAddressOf())));

	//Create a DSV to this depth buffer.
	D3D12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle = GetCPUDescriptorHandleForDepthStencilBuffer();
	Device->CreateDepthStencilView(DepthStencilBufferResource.Get(), nullptr, DSVCpuHandle);

	//Viewport
	Viewport = {};
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;
	Viewport.Width = static_cast<float>(ScreenWidth);
	Viewport.Height = static_cast<float>(ScreenHeight);
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;

	//Transition the depth/stencil resource ready for use...
	D3D12_RESOURCE_BARRIER DepthStencilResourceTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		DepthStencilBufferResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	CommandList.Get()->ResourceBarrier(1, &DepthStencilResourceTransition);

	//Close command list before execution
	CommandList.Get()->Close();

	ID3D12CommandList* CommandListsToSubmit[] = { CommandList.Get() };
	DirectGraphicsCommandQueue.Get()->ExecuteCommandLists(1, CommandListsToSubmit);

	//Wait for it to finish...
	FlushCommandQueue();

	return true;
}

bool InitScene()
{
	return true;
}

void UpdateScene(float Delta)
{}

void RenderScene()
{
	//Reuse the command list
	CheckHResult(DirectGraphicsCommandListAllocator->Reset());
	CheckHResult(CommandList.Get()->Reset(DirectGraphicsCommandListAllocator.Get(), nullptr));

	//Transition backbuffer from present to render target. 
	D3D12_RESOURCE_BARRIER RenderTargetTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		SwapchainColourBuffers[CurrentSwapchainColourBufferIdx].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CommandList.Get()->ResourceBarrier(1, &RenderTargetTransition);

	//Reset viewport
	CommandList.Get()->RSSetViewports(1, &Viewport);

	//Clear the backbuffer RTV
	static const float SwapchainRenderTargetClear[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
	CommandList.Get()->ClearRenderTargetView(
		GetCPUDescriptorHandleForSwapchainColourBuffer(CurrentSwapchainColourBufferIdx),
		SwapchainRenderTargetClear, 0, nullptr);

	//Set OM render target for rendering. 
	CommandList.Get()->OMSetRenderTargets(1, 
		&GetCPUDescriptorHandleForSwapchainColourBuffer(CurrentSwapchainColourBufferIdx),
		true, &GetCPUDescriptorHandleForDepthStencilBuffer());

	//Transition for render target -> From render target to present. 
	RenderTargetTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		SwapchainColourBuffers[CurrentSwapchainColourBufferIdx].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	CommandList.Get()->ResourceBarrier(1, &RenderTargetTransition);

	//Close command list
	CheckHResult(CommandList.Get()->Close());

	//Add command list for execution
	ID3D12CommandList* CommandListsToSubmit[] = { CommandList.Get() };
	DirectGraphicsCommandQueue.Get()->ExecuteCommandLists(1, CommandListsToSubmit);

	//Present
	CheckHResult(Swapchain->Present(0, 0));

	//Switch active back buffers
	CurrentSwapchainColourBufferIdx = (CurrentSwapchainColourBufferIdx + 1) % SwapchainBufferCount;

	//Wait for GPU to finish before rendering next frame... TODO: Improve
	FlushCommandQueue();
}

int PreShutdown()
{
	if (Device)
	{
		//Flush command queue before shutting resources down
		FlushCommandQueue();
	}

	return 0;
}

int ShutdownScene()
{
	return 0;
}

int ShutdownEngine()
{
	return 0;
}

int APIENTRY WinMain(HINSTANCE Instance, HINSTANCE PrevInstance,
	LPSTR CmdLine, int CmdShow)
{
	//Create a window
	Assert(InitWindow(Instance, PrevInstance, CmdLine, CmdShow));

	//Init D3D12
	Assert(InitD3D12());

	//Init scene
	Assert(InitScene());

	//Init game timer
	GameTimer Timer;
	Timer.Reset();

	//Run
	while (gQuit == false)
	{
		MSG Message;
		while (PeekMessage(&Message, Window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessage(&Message);
		}
		
		Timer.Tick();
		UpdateScene(Timer.DeltaTime());
		RenderScene();
		
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			PostMessage(Window, WM_QUIT, 0, 0);
		}
	}

	//Close
	Assert(PreShutdown() == 0)
	Assert(ShutdownScene() == 0);
	Assert(ShutdownEngine() == 0);
	return 0;
}
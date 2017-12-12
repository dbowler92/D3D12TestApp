#include <windows.h>
#include <wrl.h> 

#include <dxgi1_4.h>
#include <d3d12.h>

#include <dxgidebug.h>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

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

//Global settings
const unsigned SwapchainBufferCount = 2;
const DXGI_FORMAT SwapchainBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
const bool bEnableMSAA = true; //TODO:
const unsigned MSAACount = 4;  //TODO:

//App info
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

UINT64 RTVDescriptorStride;
UINT64 DSVDescriptorStride;
UINT64 CBVDescriptorStride;


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

	CommandList->Close();

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



	return true;
}

bool InitScene()
{
	return true;
}

void UpdateScene(float Delta)
{
}

void RenderScene()
{

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
	Assert(ShutdownScene() == 0);
	Assert(ShutdownEngine() == 0);
	return 0;
}
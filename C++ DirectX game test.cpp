#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

HWND g_hWnd = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTargetView = nullptr;
ID3D11InputLayout* g_inputLayout = nullptr;
ID3D11Buffer* g_vertexBuffer = nullptr;
ID3D11VertexShader* g_vertexShader = nullptr;
ID3D11PixelShader* g_pixelShader = nullptr;
ID3D11Buffer* g_constantBuffer = nullptr;

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT3 color;
};

struct CB {
    XMMATRIX viewProj;
};

XMFLOAT3 g_cameraPos = { 0.0f, 1.0f, -5.0f };
float g_yaw = 0.0f, g_pitch = 0.0f;
bool g_keys[256] = { false };
POINT g_lastMouse = {};
std::chrono::steady_clock::time_point g_lastTime;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); break;
    case WM_KEYDOWN: g_keys[wParam] = true; break;
    case WM_KEYUP: g_keys[wParam] = false; break;
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            int dx = LOWORD(lParam) - g_lastMouse.x;
            int dy = HIWORD(lParam) - g_lastMouse.y;
            g_yaw += dx * 0.005f;
            g_pitch += dy * 0.005f;
            g_pitch = max(-XM_PIDIV2, min(XM_PIDIV2, g_pitch));
        }
        g_lastMouse.x = LOWORD(lParam);
        g_lastMouse.y = HIWORD(lParam);
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool InitD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = 800;
    scd.BufferDesc.Height = 600;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, &scd, &g_swapChain,
        &g_device, nullptr, &g_context))) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
    backBuffer->Release();

    g_context->OMSetRenderTargets(1, &g_renderTargetView, nullptr);

    D3D11_VIEWPORT vp = { 0, 0, 800, 600, 0.0f, 1.0f };
    g_context->RSSetViewports(1, &vp);

    // Compile shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    const char* vsSrc =
        "cbuffer CB : register(b0) { matrix viewProj; };"
        "struct VS_IN { float3 pos : POSITION; float3 col : COLOR; };"
        "struct VS_OUT { float4 pos : SV_POSITION; float3 col : COLOR; };"
        "VS_OUT main(VS_IN input) {"
        "  VS_OUT o; o.pos = mul(float4(input.pos,1),viewProj); o.col = input.col; return o; }";
    const char* psSrc =
        "struct PS_IN { float4 pos : SV_POSITION; float3 col : COLOR; };"
        "float4 main(PS_IN input) : SV_TARGET { return float4(input.col,1); }";

    D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, nullptr);

    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vertexShader);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_inputLayout);
    g_context->IASetInputLayout(g_inputLayout);

    vsBlob->Release(); psBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CB);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_device->CreateBuffer(&cbd, nullptr, &g_constantBuffer);

    std::vector<Vertex> verts;
    for (int x = -10; x < 10; ++x) {
        for (int z = -10; z < 10; ++z) {
            bool isRed = ((x + z) % 2) == 0;
            XMFLOAT3 color = isRed ? XMFLOAT3(1, 0, 0) : XMFLOAT3(0, 1, 0);
            float fx = (float)x, fz = (float)z;
            verts.push_back({ {fx, 0, fz}, color });
            verts.push_back({ {fx + 1, 0, fz}, color });
            verts.push_back({ {fx, 0, fz + 1}, color });
            verts.push_back({ {fx + 1, 0, fz}, color });
            verts.push_back({ {fx + 1, 0, fz + 1}, color });
            verts.push_back({ {fx, 0, fz + 1}, color });
        }
    }

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = UINT(sizeof(Vertex) * verts.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = { verts.data() };
    g_device->CreateBuffer(&vbd, &initData, &g_vertexBuffer);

    return true;
}

void UpdateCamera(float dt) {
    XMVECTOR forward = XMVectorSet(sinf(g_yaw), 0, cosf(g_yaw), 0);
    XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward);
    XMVECTOR pos = XMLoadFloat3(&g_cameraPos);

    if (g_keys['W']) pos += forward * dt;
    if (g_keys['S']) pos -= forward * dt;
    if (g_keys['A']) pos -= right * dt;
    if (g_keys['D']) pos += right * dt;

    XMStoreFloat3(&g_cameraPos, pos);
}

void RenderFrame() {
    const float bgColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f }; // Azul oscuro
    g_context->ClearRenderTargetView(g_renderTargetView, bgColor);

    XMVECTOR eye = XMVectorSet(0.0f, 3.0f, -5.0f, 0.0f);  // Altura de 3m
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);     // Mirar al centro del plano
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, 800 / 600.0f, 0.1f, 100.0f);

    CB cb;
    cb.viewProj = XMMatrixTranspose(view * proj);
    g_context->UpdateSubresource(g_constantBuffer, 0, nullptr, &cb, 0, 0);

    UINT stride = sizeof(Vertex), offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_context->VSSetShader(g_vertexShader, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_constantBuffer);
    g_context->PSSetShader(g_pixelShader, nullptr, 0);

    g_context->Draw(2400, 0);  // 400 cuadros × 6 vértices

    g_swapChain->Present(1, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    WNDCLASS wc = { CS_OWNDC, WndProc, 0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"D3DWindow" };
    RegisterClass(&wc);
    g_hWnd = CreateWindow(L"D3DWindow", L"DirectX 11 - Chess Ground", WS_OVERLAPPEDWINDOW,
        100, 100, 800, 600, nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hWnd, SW_SHOW);

    if (!InitD3D(g_hWnd)) return 0;

    g_lastTime = std::chrono::steady_clock::now();

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - g_lastTime).count();
            g_lastTime = now;

            UpdateCamera(dt);
            RenderFrame();
        }
    }

    return 0;
}
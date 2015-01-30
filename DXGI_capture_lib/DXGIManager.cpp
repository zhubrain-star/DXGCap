#include "DXGIManager.hpp"


DuplicatedOutput::DuplicatedOutput(ID3D11Device* device,
	ID3D11DeviceContext* context,
	IDXGIOutput1* output,
	IDXGIOutputDuplication* output_dup):
		m_device(device),
		m_device_context(context),
		m_output(output),
		m_dxgi_output_dup(output_dup) { }

void DuplicatedOutput::get_desc(DXGI_OUTPUT_DESC& desc) {
	m_output->GetDesc(&desc);
}

HRESULT DuplicatedOutput::acquire_next_frame(IDXGISurface1** pDXGISurface) {
	DXGI_OUTDUPL_FRAME_INFO fi;
	CComPtr<IDXGIResource> spDXGIResource;
	HRESULT hr = m_dxgi_output_dup->AcquireNextFrame(20, &fi, &spDXGIResource);
	if (FAILED(hr)) {
		printf("m_DXGIOutputDuplication->AcquireNextFrame failed with hr=0x%08x\n", hr);
		return hr;
	}

	CComQIPtr<ID3D11Texture2D> spTextureResource = spDXGIResource;

	D3D11_TEXTURE2D_DESC desc;
	spTextureResource->GetDesc(&desc);

	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.Width = desc.Width;
	texDesc.Height = desc.Height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_STAGING;
	texDesc.Format = desc.Format;
	texDesc.BindFlags = 0;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	texDesc.MiscFlags = 0;

	CComPtr<ID3D11Texture2D> spD3D11Texture2D = NULL;
	hr = m_device->CreateTexture2D(&texDesc, NULL, &spD3D11Texture2D);
	if (FAILED(hr)) {
		return hr;
	}

	m_device_context->CopyResource(spD3D11Texture2D, spTextureResource);

	CComQIPtr<IDXGISurface1> spDXGISurface = spD3D11Texture2D;

	*pDXGISurface = spDXGISurface.Detach();

	return hr;
}

void DuplicatedOutput::release_frame() {
	m_dxgi_output_dup->ReleaseFrame();
}

bool DuplicatedOutput::is_primary() {
	DXGI_OUTPUT_DESC outdesc;
	m_output->GetDesc(&outdesc);

	MONITORINFO mi;
	mi.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(outdesc.Monitor, &mi);
	if (mi.dwFlags & MONITORINFOF_PRIMARY) {
		return true;
	}
	return false;
}

DXGIManager::DXGIManager() {
	m_capture_rect = CSUndefined;
	SetRect(&m_current_output, 0, 0, 0, 0);
	m_buf = NULL;
	m_initialized = false;
}

DXGIManager::~DXGIManager() {
	if (m_buf) {
		delete[] m_buf;
		m_buf = NULL;
	}
}

void DXGIManager::set_capture_source(CaptureSource cs) {
	m_capture_rect = cs;
}
CaptureSource DXGIManager::get_capture_source() {
	return m_capture_rect;
}

HRESULT DXGIManager::init() {
	if (m_initialized) {
		return S_OK;
	}

	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&m_factory));
	if (FAILED(hr)) {
		printf("Failed to CreateDXGIFactory1 hr=%08x\n", hr);
		return hr;
	}

	// Getting all adapters
	vector<CComPtr<IDXGIAdapter1>> vAdapters;

	CComPtr<IDXGIAdapter1> spAdapter;
	for (int i = 0; m_factory->EnumAdapters1(i, &spAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
		vAdapters.push_back(spAdapter);
		spAdapter.Release();
	}

	// Iterating over all adapters to get all outputs
	for (vector<CComPtr<IDXGIAdapter1>>::iterator AdapterIter = vAdapters.begin();
		AdapterIter != vAdapters.end();
		AdapterIter++) {
		vector<CComPtr<IDXGIOutput>> vOutputs;

		CComPtr<IDXGIOutput> spDXGIOutput;
		for (int i = 0; (*AdapterIter)->EnumOutputs(i, &spDXGIOutput) != DXGI_ERROR_NOT_FOUND; i++) {
			DXGI_OUTPUT_DESC outputDesc;
			spDXGIOutput->GetDesc(&outputDesc);

			printf("Display output found. DeviceName=%ls  AttachedToDesktop=%d Rotation=%d DesktopCoordinates={(%d,%d),(%d,%d)}\n",
				outputDesc.DeviceName,
				outputDesc.AttachedToDesktop,
				outputDesc.Rotation,
				outputDesc.DesktopCoordinates.left,
				outputDesc.DesktopCoordinates.top,
				outputDesc.DesktopCoordinates.right,
				outputDesc.DesktopCoordinates.bottom);

			if (outputDesc.AttachedToDesktop) {
				vOutputs.push_back(spDXGIOutput);
			}

			spDXGIOutput.Release();
		}

		if (vOutputs.size() == 0) {
			continue;
		}

		// Creating device for each adapter that has the output
		CComPtr<ID3D11Device> spD3D11Device;
		CComPtr<ID3D11DeviceContext> spD3D11DeviceContext;
		D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_9_1;
		hr = D3D11CreateDevice((*AdapterIter), D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &spD3D11Device, &fl, &spD3D11DeviceContext);
		if (FAILED(hr)) {
			printf("Failed to create D3D11CreateDevice hr=%08x\n", hr);
			return hr;
		}

		for (std::vector<CComPtr<IDXGIOutput>>::iterator OutputIter = vOutputs.begin();
			OutputIter != vOutputs.end();
			OutputIter++) {
			CComQIPtr<IDXGIOutput1> spDXGIOutput1 = *OutputIter;
			CComQIPtr<IDXGIDevice1> spDXGIDevice = spD3D11Device;
			if (!spDXGIOutput1 || !spDXGIDevice) {
				continue;
			}

			CComPtr<IDXGIOutputDuplication> spDuplicatedOutput;
			hr = spDXGIOutput1->DuplicateOutput(spDXGIDevice, &spDuplicatedOutput);
			if (FAILED(hr)) {
				continue;
			}

			m_outputs.push_back(
				DuplicatedOutput(spD3D11Device,
					spD3D11DeviceContext,
					spDXGIOutput1,
					spDuplicatedOutput));
		}
	}

	hr = m_spWICFactory.CoCreateInstance(CLSID_WICImagingFactory);
	if (FAILED(hr)) {
		printf("Failed to create WICImagingFactory hr=%08x\n", hr);
		return hr;
	}

	m_initialized = true;

	return S_OK;
}

HRESULT DXGIManager::get_output_rect(RECT& rc) {
	// Nulling rc just in case...
	SetRect(&rc, 0, 0, 0, 0);

	HRESULT hr = init();
	if (hr != S_OK) {
		return hr;
	}

	vector<DuplicatedOutput> vOutputs = get_output_duplication();

	RECT rcShare;
	SetRect(&rcShare, 0, 0, 0, 0);

	for (vector<DuplicatedOutput>::iterator iter = vOutputs.begin();
		iter != vOutputs.end();
		iter++) {
		DuplicatedOutput& out = *iter;

		DXGI_OUTPUT_DESC outDesc;
		out.get_desc(outDesc);
		RECT rcOutCoords = outDesc.DesktopCoordinates;

		UnionRect(&rcShare, &rcShare, &rcOutCoords);
	}

	CopyRect(&rc, &rcShare);

	return S_OK;
}

HRESULT DXGIManager::get_output_bits(BYTE* pBits, RECT& rcDest) {
	HRESULT hr = S_OK;

	DWORD dwDestWidth = rcDest.right - rcDest.left;
	DWORD dwDestHeight = rcDest.bottom - rcDest.top;

	RECT rcOutput;
	hr = get_output_rect(rcOutput);
	if (FAILED(hr)) {
		return hr;
	}

	DWORD dwOutputWidth = rcOutput.right - rcOutput.left;
	DWORD dwOutputHeight = rcOutput.bottom - rcOutput.top;

	BYTE* pBuf = NULL;
	if (rcOutput.right > (LONG)dwDestWidth || rcOutput.bottom > (LONG)dwDestHeight) {
		// Output is larger than pBits dimensions
		if (!m_buf || !EqualRect(&m_current_output, &rcOutput)) {
			DWORD dwBufSize = dwOutputWidth*dwOutputHeight * 4;

			if (m_buf) {
				delete[] m_buf;
				m_buf = NULL;
			}

			m_buf = new BYTE[dwBufSize];

			CopyRect(&m_current_output, &rcOutput);
		}

		pBuf = m_buf;
	}
	else {
		// Output is smaller than pBits dimensions
		pBuf = pBits;
		dwOutputWidth = dwDestWidth;
		dwOutputHeight = dwDestHeight;
	}

	vector<DuplicatedOutput> vOutputs = get_output_duplication();

	for (vector<DuplicatedOutput>::iterator iter = vOutputs.begin();
		iter != vOutputs.end();
		iter++) {
		DuplicatedOutput& out = *iter;

		DXGI_OUTPUT_DESC outDesc;
		out.get_desc(outDesc);
		RECT rcOutCoords = outDesc.DesktopCoordinates;

		CComPtr<IDXGISurface1> spDXGISurface1;
		hr = out.acquire_next_frame(&spDXGISurface1);
		if (FAILED(hr)) {
			break;
		}

		DXGI_MAPPED_RECT map;
		spDXGISurface1->Map(&map, DXGI_MAP_READ);

		RECT rcDesktop = outDesc.DesktopCoordinates;
		DWORD dwWidth = rcDesktop.right - rcDesktop.left;
		DWORD dwHeight = rcDesktop.bottom - rcDesktop.top;

		OffsetRect(&rcDesktop, -rcOutput.left, -rcOutput.top);

		DWORD dwMapPitchPixels = map.Pitch / 4;

		switch (outDesc.Rotation) {
		case DXGI_MODE_ROTATION_IDENTITY: {
			// Just copying
			DWORD dwStripe = dwWidth * 4;
			for (unsigned int i = 0; i<dwHeight; i++) {
				memcpy_s(pBuf + (rcDesktop.left + (i + rcDesktop.top)*dwOutputWidth) * 4, dwStripe, map.pBits + i*map.Pitch, dwStripe);
			}
		}
		break;
		case DXGI_MODE_ROTATION_ROTATE90: {
			// Rotating at 90 degrees
			DWORD* pSrc = (DWORD*)map.pBits;
			DWORD* pDst = (DWORD*)pBuf;
			for (unsigned int j = 0; j<dwHeight; j++) {
				for (unsigned int i = 0; i<dwWidth; i++) {
					*(pDst + (rcDesktop.left + (j + rcDesktop.top)*dwOutputWidth) + i) = *(pSrc + j + dwMapPitchPixels*(dwWidth - i - 1));
				}
			}
		}
		break;
		case DXGI_MODE_ROTATION_ROTATE180: {
			// Rotating at 180 degrees
			DWORD* pSrc = (DWORD*)map.pBits;
			DWORD* pDst = (DWORD*)pBuf;
			for (unsigned int j = 0; j<dwHeight; j++) {
				for (unsigned int i = 0; i<dwWidth; i++) {
					*(pDst + (rcDesktop.left + (j + rcDesktop.top)*dwOutputWidth) + i) = *(pSrc + (dwWidth - i - 1) + dwMapPitchPixels*(dwHeight - j - 1));
				}
			}
		}
		break;
		case DXGI_MODE_ROTATION_ROTATE270: {
			// Rotating at 270 degrees
			DWORD* pSrc = (DWORD*)map.pBits;
			DWORD* pDst = (DWORD*)pBuf;
			for (unsigned int j = 0; j<dwHeight; j++) {
				for (unsigned int i = 0; i<dwWidth; i++) {
					*(pDst + (rcDesktop.left + (j + rcDesktop.top)*dwOutputWidth) + i) = *(pSrc + (dwHeight - j - 1) + dwMapPitchPixels*i);
				}
			}
		}
		break;
		}

		spDXGISurface1->Unmap();

		out.release_frame();
	}

	if (FAILED(hr)) {
		return hr;
	}


	// We have the pBuf filled with current desktop/monitor image.
	if (pBuf != pBits) {
		// pBuf contains the image that should be resized
		CComPtr<IWICBitmap> spBitmap = NULL;
		hr = m_spWICFactory->CreateBitmapFromMemory(dwOutputWidth, dwOutputHeight, GUID_WICPixelFormat32bppBGRA, dwOutputWidth * 4, dwOutputWidth*dwOutputHeight * 4, (BYTE*)pBuf, &spBitmap);
		if (FAILED(hr)) {
			return hr;
		}

		CComPtr<IWICBitmapScaler> spBitmapScaler = NULL;
		hr = m_spWICFactory->CreateBitmapScaler(&spBitmapScaler);
		if (FAILED(hr)) {
			return hr;
		}

		dwOutputWidth = rcOutput.right - rcOutput.left;
		dwOutputHeight = rcOutput.bottom - rcOutput.top;

		double aspect = (double)dwOutputWidth / (double)dwOutputHeight;

		DWORD scaledWidth = dwDestWidth;
		DWORD scaledHeight = dwDestHeight;

		if (aspect > 1) {
			scaledWidth = dwDestWidth;
			scaledHeight = (DWORD)(dwDestWidth / aspect);
		}
		else {
			scaledWidth = (DWORD)(aspect*dwDestHeight);
			scaledHeight = dwDestHeight;
		}

		spBitmapScaler->Initialize(
			spBitmap, scaledWidth, scaledHeight, WICBitmapInterpolationModeNearestNeighbor);

		spBitmapScaler->CopyPixels(NULL, scaledWidth * 4, dwDestWidth*dwDestHeight * 4, pBits);
	}
	return hr;
}

vector<DuplicatedOutput> DXGIManager::get_output_duplication() {
	vector<DuplicatedOutput> outputs;
	switch (m_capture_rect) {
	case CSMonitor1: {
		// Return the one with is_primary
		for (vector<DuplicatedOutput>::iterator iter = m_outputs.begin();
			iter != m_outputs.end();
			iter++) {
			DuplicatedOutput& out = *iter;
			if (out.is_primary()) {
				outputs.push_back(out);
				break;
			}
		}
	}
	break;

	case CSMonitor2: {
		// Return the first with !is_primary
		for (vector<DuplicatedOutput>::iterator iter = m_outputs.begin();
			iter != m_outputs.end();
			iter++) {
			DuplicatedOutput& out = *iter;
			if (!out.is_primary()) {
				outputs.push_back(out);
				break;
			}
		}
	}
	break;

	case CSDesktop: {
		// Return all outputs
		for (vector<DuplicatedOutput>::iterator iter = m_outputs.begin();
			iter != m_outputs.end();
			iter++) {
			DuplicatedOutput& out = *iter;
			outputs.push_back(out);
		}
	}
	break;
	}
	return outputs;
}
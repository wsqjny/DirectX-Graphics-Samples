set DXC_PATH=D:\JNLearn\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\HelloTexture\ThirdParty\dxc\bin\x64\dxc.exe

:: %DXC_PATH% Raytracing.hlsl -T lib_6_3 -E main -Zi -Od -Fo output\Raytracing.cso -Fh output\Raytracing.hlsl.h -Vn g_pRaytracing -nologo

:: with payload access qualifiers
:: %DXC_PATH% Raytracing_SM69.hlsl -T lib_6_9 -E main -Zi -Od -Fo output_sm69\Raytracing.cso -Fh output_sm69\Raytracing.hlsl.h -Vn g_pRaytracing -nologo

%DXC_PATH% Raytracing_SM69.hlsl -T lib_6_9 -E main -Zi -Od -Fo output_sm69\Raytracing.cso -Fh output_sm69\Raytracing.hlsl.h -Vn g_pRaytracing -nologo -disable-payload-qualifiers

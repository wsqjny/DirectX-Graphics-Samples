@echo off

set DXC_PATH=D:\JNLearn\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\HelloTexture\ThirdParty\dxc\bin\x64\dxc.exe
set SHADER_FILE=.\Shaders\shaders.hlsl
set OUT_DIR=bin\x64\Debug\compiled

mkdir %OUT_DIR%

%DXC_PATH% -T vs_6_6 -E VSMain -HV 2021 -enable-16bit-types -Fo %OUT_DIR%\VSMain.cso %SHADER_FILE%
%DXC_PATH% -T ps_6_6 -E PSMain -HV 2021 -enable-16bit-types -Fo %OUT_DIR%\PSMain.cso %SHADER_FILE%


%DXC_PATH% -T cs_6_6 -E UEMain -HV 2021 -enable-16bit-types -I .\ThirdParty\libntc\include\ -Fo %OUT_DIR%\NtcDecompressCSMain.dxil .\Shaders\NtcDeompress.hlsl


echo Shaders compiled.
pause

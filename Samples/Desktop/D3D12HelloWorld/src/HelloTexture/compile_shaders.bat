@echo off

set DXC_PATH=D:\JNLearn\DirectX-Graphics-Samples\Samples\Desktop\D3D12HelloWorld\src\HelloTexture\ThirdParty\dxc\bin\x64\dxc.exe
set SHADER_FILE=shaders.hlsl
set OUT_DIR=bin\x64\Debug\compiled

mkdir %OUT_DIR%

%DXC_PATH% -T vs_6_9 -E VSMain -HV 2021 -enable-16bit-types -Fo %OUT_DIR%\VSMain.cso %SHADER_FILE%
%DXC_PATH% -T ps_6_9 -E PSMain -HV 2021 -enable-16bit-types -Fo %OUT_DIR%\PSMain.cso %SHADER_FILE%


%DXC_PATH%  -HV 2021 -Zpr -O3 -auto-binding-space 0 -Wno-parentheses-equality -disable-lifetime-markers -disable-payload-qualifiers -T cs_6_9 -E FLumenVisualizeCreateTilesCS -Fo %OUT_DIR%\FLumenVisualizeCreateTilesCS.dxil shaders\FLumenVisualizeCreateTilesCS.usf
%DXC_PATH%  -HV 2021 -Zpr -O3 -auto-binding-space 0 -Wno-parentheses-equality -disable-lifetime-markers -disable-payload-qualifiers -T cs_6_9 -E FLumenVisualizeCreateRaysCS -Fo %OUT_DIR%\FLumenVisualizeCreateRaysCS.dxil shaders\FLumenVisualizeCreateRaysCS.usf

echo Shaders compiled.
pause

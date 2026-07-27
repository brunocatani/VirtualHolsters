param(
    [Parameter(Mandatory = $true)]
    [string]$Root
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Failure
    )
    if ($Text -notmatch $Pattern) {
        throw $Failure
    }
}

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Failure
    )
    if ($Text -match $Pattern) {
        throw $Failure
    }
}

$main = Get-Content -LiteralPath (Join-Path $Root "main.cpp") -Raw
$hook = Get-Content -LiteralPath (Join-Path $Root "hook.cpp") -Raw
$vrHeader = Get-Content -LiteralPath (Join-Path $Root "VRHook.h") -Raw
$vrSource = Get-Content -LiteralPath (Join-Path $Root "VRHook.cpp") -Raw
$cmake = Get-Content -LiteralPath (Join-Path $Root "CMakeLists.txt") -Raw
$exports = Get-Content -LiteralPath (Join-Path $Root "exports.def") -Raw

$productionFiles = Get-ChildItem -LiteralPath $Root -File |
    Where-Object { $_.Extension -in @(".h", ".hpp", ".cpp") }
$productionText = ($productionFiles |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join
    [Environment]::NewLine

Assert-Contains $main 'REL::Module::IsVR\(\)' "Query must reject non-VR runtimes."
Assert-Contains $main 'const auto requiredRuntime = F4SE::RUNTIME_1_10_138;' "Query must use the F4SEVR loader compatibility value."
Assert-Contains $main 'RuntimeVersion\(\)\s*<\s*requiredRuntime' "Query must enforce the loader compatibility minimum."
Assert-Contains $main 'REL::Module::get\(\)\.version\(\)' "Query must inspect the executable file version through REL."
Assert-Contains $main 'executableVersion\s*!=\s*F4SE::RUNTIME_VR_1_2_72' "Query must require Fallout4VR.exe 1.2.72."
Assert-NotContains $main 'RuntimeVersion\(\)\s*(?:==|!=|<=|>=|<|>)\s*F4SE::RUNTIME_VR_1_2_72' "QueryInterface::RuntimeVersion must not be compared with the executable-version constant."
Assert-Contains $main '(?s)F4SEPlugin_Query\(.+?\)\s+noexcept' "Query must not propagate exceptions."
Assert-Contains $main '(?s)F4SEPlugin_Load\(.+?\)\s+noexcept' "Load must not propagate exceptions."
Assert-Contains $main 'F4SE::Init\(a_f4se,\s*false\)' "Load must initialize CommonLibF4VR."
Assert-Contains $main 'kPreLoadGame' "Lifecycle must close the gameplay gate before loading a save."
Assert-Contains $main 'kPostLoadGame' "Lifecycle must rearm after loading a save."

Assert-Contains $hook "0x00D8'405E" "The unsafe Windows message-pump hook must be replaced by the verified frame callsite."
Assert-Contains $hook "0x00D3'C820" "The verified vanilla call target must remain explicit."
Assert-Contains $hook '0x48,\s*0x8B,\s*0x0D,\s*0x8A,\s*0x1C,\s*0xD5,\s*0x04' "The verified callsite prefix must remain build-enforced."
Assert-Contains $hook '0x0F,\s*0x0D,\s*0x89,\s*0xCC,\s*0x07,\s*0x00' "The verified vanilla callee prefix must remain build-enforced."
Assert-Contains $hook 'using OriginalFunction = bool \(\*\)\(void\* state\);' "The frame hook must retain the blind-verified bool(void*) ABI."
Assert-Contains $hook 'InspectCallsite' "The frame callsite must be inspected before mutation."
Assert-Contains $hook 'DecodeRelativeCallTarget' "The current hook-chain predecessor must be decoded before mutation."
Assert-Contains $hook 'IsExecutableAddress' "A chained predecessor must point to executable memory."
Assert-Contains $hook 'write_call<5>' "The frame integration must preserve ordinary callsite chaining."
Assert-NotContains $hook '0xd8187e|0xD8187E' "The Windows message-pump callsite must never return."

Assert-Contains $vrSource 'vr::VRSystem\(\)' "OpenVR access must be borrowed from the process."
Assert-Contains $vrSource 'TriggerHapticPulse' "Haptics must use the direct OpenVR ABI."
Assert-NotContains $productionText 'FO4VRTools|RequestOpenVRHookManagerObject|GetVRHookManager' "Production source must not depend on FO4VRTools."
Assert-NotContains ($vrHeader + $vrSource) '\bVR_Init\s*\(|\bVR_Shutdown\s*\(' "VirtualHolsters must not own OpenVR initialization or shutdown."

Assert-Contains $cmake 'CommonLibF4::CommonLibF4' "The plugin must link CommonLibF4VR."
Assert-Contains $cmake 'OpenVR::OpenVR' "The plugin must link its independently vendored OpenVR ABI."
Assert-Contains $cmake 'VirtualHolstersSourceRegression' "The source regression must be enforced by the normal build."
Assert-Contains $cmake 'POST_BUILD_COPY_PLUGIN' "The auto-deploying build path must remain available."
Assert-NotContains $cmake 'INTERPROCEDURAL_OPTIMIZATION_RELEASE\s+TRUE' "LTO must remain disabled."

Assert-Contains $exports '(?m)^\s*F4SEPlugin_Query\s*$' "F4SEPlugin_Query must be exported."
Assert-Contains $exports '(?m)^\s*F4SEPlugin_Load\s*$' "F4SEPlugin_Load must be exported."
Assert-Contains $exports '(?m)^\s*VHAPI_GetApi\s*$' "The existing VirtualHolsters API must remain exported."

$obsoleteBuildPaths = @(
    "VirtualHolsters.sln",
    "VirtualHolsters.vcxproj",
    "VirtualHolsters.vcxproj.filters",
    "VRHookAPI.h"
)
foreach ($relativePath in $obsoleteBuildPaths) {
    if (Test-Path -LiteralPath (Join-Path $Root $relativePath)) {
        throw "Obsolete unsafe build/dependency path still exists: $relativePath"
    }
}

Write-Host "All VirtualHolsters source regressions passed."

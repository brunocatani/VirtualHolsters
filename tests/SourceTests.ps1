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
$holsters = Get-Content -LiteralPath (Join-Path $Root "Holsters.cpp") -Raw
$configMode = Get-Content -LiteralPath (Join-Path $Root "ConfigModeMenu.cpp") -Raw
$inventory = Get-Content -LiteralPath (Join-Path $Root "InvSorting.cpp") -Raw
$legacyBridge = Get-Content -LiteralPath (Join-Path $Root "LegacyBridge.cpp") -Raw
$interaction = Get-Content -LiteralPath (Join-Path $Root "HolsterInteraction.h") -Raw
$slotPolicy = Get-Content -LiteralPath (Join-Path $Root "HolsterSlotPolicy.h") -Raw
$vrHeader = Get-Content -LiteralPath (Join-Path $Root "VRHook.h") -Raw
$vrSource = Get-Content -LiteralPath (Join-Path $Root "VRHook.cpp") -Raw
$sceneGraphHeader = Get-Content -LiteralPath (Join-Path $Root "VRSceneGraph.h") -Raw
$sceneGraphSource = Get-Content -LiteralPath (Join-Path $Root "VRSceneGraph.cpp") -Raw
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
Assert-Contains $main 'OnGameplaySuspended\(\)' "Pre-load lifecycle must cancel active holster gestures."

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

Assert-Contains $sceneGraphSource 'kAttachChildSlot\s*=\s*0x3D' "FO4VR NiNode AttachChild must use verified slot 0x3D."
Assert-Contains $sceneGraphSource 'kRemoveChildSlot\s*=\s*0x40' "FO4VR NiNode RemoveChild must use verified slot 0x40."
Assert-Contains $sceneGraphSource "kAttachChildRva\s*=\s*0x01C1'7E20" "FO4VR NiNode AttachChild identity must remain explicit."
Assert-Contains $sceneGraphSource "kRemoveChildRva\s*=\s*0x01C1'7F70" "FO4VR NiNode RemoveChild identity must remain explicit."
Assert-Contains $sceneGraphSource '0x48,\s*0x85,\s*0xD2,\s*0x0F,\s*0x84,\s*0xBF' "AttachChild live-byte verification must remain build-enforced."
Assert-Contains $sceneGraphSource '0x48,\s*0x83,\s*0xEC,\s*0x28,\s*0x48,\s*0x8B,\s*0x01' "RemoveChild live-byte verification must remain build-enforced."
Assert-Contains $sceneGraphSource 'RE::NiNode::VTABLE\[0\]\.address\(\)' "NiNode ABI validation must resolve the authoritative address-library vtable."
Assert-Contains $sceneGraphSource 'VerifyBaseVtableEntry' "NiNode slots must be checked against the live base vtable before gameplay."
Assert-Contains $sceneGraphSource 'segment\(REL::Segment::text\)' "Every runtime NiNode dispatch target must remain inside Fallout4VR executable code."
Assert-Contains $sceneGraphSource 'IsExecutableGameTextAddress\(target\)' "Derived NiNode dispatches must fail closed outside executable game code."
Assert-Contains $legacyBridge 'VRSceneGraph::Initialize\(\)' "GameLoaded must verify the FO4VR scene-graph ABI before legacy gameplay starts."
Assert-Contains ($holsters + $configMode) 'VRSceneGraph::AttachChild' "Legacy scene-graph attachment sites must use the FO4VR adapter."
Assert-Contains ($holsters + $configMode) 'VRSceneGraph::RemoveChild' "Legacy scene-graph removal sites must use the FO4VR adapter."
Assert-NotContains $productionText '->\s*(?:AttachChild|InsertChildAt|DetachChild|RemoveChild|DetachChildAt|RemoveChildAt|SetAt|UpdateWorldData|UpdateTransformAndBounds|UpdateTransforms|UpdateUpwardPass)\s*\(' "Legacy production sources must not dispatch shifted flat-layout scene-graph virtuals directly."
Assert-Contains $sceneGraphHeader '(?s)AttachChild\(.+?\)\s+noexcept' "The scene-graph adapter must not propagate exceptions."
Assert-Contains $sceneGraphHeader 'RemoveChild\(.+?\)\s+noexcept' "The scene-graph adapter must not propagate exceptions."

Assert-Contains $vrSource 'vr::VRSystem\(\)' "OpenVR access must be borrowed from the process."
Assert-Contains $vrSource 'TriggerHapticPulse' "Haptics must use the direct OpenVR ABI."
Assert-NotContains $productionText 'FO4VRTools|RequestOpenVRHookManagerObject|GetVRHookManager' "Production source must not depend on FO4VRTools."
Assert-NotContains ($vrHeader + $vrSource) '\bVR_Init\s*\(|\bVR_Shutdown\s*\(' "VirtualHolsters must not own OpenVR initialization or shutdown."
Assert-Contains $vrHeader 'isControllerStateAvailable' "Per-hand input validity must remain explicit."

Assert-Contains $holsters 's_leftHand\.gesture\.Update' "Gameplay must update an independent left-hand gesture."
Assert-Contains $holsters 's_rightHand\.gesture\.Update' "Gameplay must update an independent right-hand gesture."
Assert-Contains $holsters 'Interaction::SelectAction' "Per-hand holster action arbitration must remain explicit."
Assert-Contains $holsters 'IsPullOutActionEligible' "Pull-out must recheck occupied-slot equip eligibility."
Assert-Contains $holsters 'leftCanArmPullOut' "Left-hand pull-out must arm only on an eligible press."
Assert-Contains $holsters 'rightCanArmPullOut' "Right-hand pull-out must arm only on an eligible press."
Assert-NotContains $holsters 'HolsterWButtonPressed\s*&&' "Holster actions must not fire from a held-button level."
Assert-Contains $interaction 'return\s*\{\s*input\.candidateSlot\s*,\s*kNoSlot\s*\};' "The release-frame zone must remain authoritative."
Assert-Contains $interaction 'input\.candidateSlot\s*!=\s*pullOutSlot_' "Pull-out must trigger only after exiting its armed slot."
Assert-Contains $interaction 'phase_\s*=\s*GesturePhase::kConsumed' "Pull-out must consume the active press."
Assert-Contains $interaction 'return\s*\{\s*kNoSlot\s*,\s*pulledSlot\s*\};' "Pull-out must emit its originally armed slot."
Assert-Contains $holsters 'isHandInHolsterZone\(isLeft\)' "The public API must preserve per-hand zone identity."
Assert-Contains $slotPolicy 'kAllSlotsMask\s*=\s*0xFE' "The slot policy must represent all seven physical holsters explicitly."
Assert-Contains $slotPolicy 'NextEnabled' "The slot policy must own enabled-slot cycling."
Assert-Contains $holsters '"Holster Slots"' "Holster availability must be loaded from its dedicated INI section."
Assert-Contains $holsters 'SlotPolicy::kConfigKeys\[slot\]' "Every physical slot must have an independent enable key."
Assert-Contains $holsters '(?s)detectHolsterSphere\(\).+?!isHolsterSlotEnabled\(slot\)' "Disabled slots must be excluded from hand-zone detection."
Assert-Contains $inventory '(?s)GameLoadInvSort\(\).+?Disabled holster association cleared' "Disabled slots must release their saved weapon association on load."
Assert-Contains $inventory '(?s)WeapSort\(int handle\).+?!isHolsterSlotEnabled\(handle\)' "Disabled slots must reject equip and holster actions."
Assert-Contains $configMode 'getNextEnabledHolster\(gCurHolster\)' "Config-mode cycling must skip disabled holsters."

Assert-Contains $cmake 'CommonLibF4::CommonLibF4' "The plugin must link CommonLibF4VR."
Assert-Contains $cmake 'OpenVR::OpenVR' "The plugin must link its independently vendored OpenVR ABI."
Assert-Contains $cmake 'VirtualHolstersSourceRegression' "The source regression must be enforced by the normal build."
Assert-Contains $cmake 'VirtualHolstersInteractionTests' "The holster gesture policy must retain executable regression coverage."
Assert-Contains $cmake 'POST_BUILD_COPY_PLUGIN' "The auto-deploying build path must remain available."
Assert-NotContains $cmake 'TARGET_PDB_FILE' "Auto-deploy must copy only VirtualHolsters.dll."
Assert-NotContains $cmake 'INTERPROCEDURAL_OPTIMIZATION_RELEASE\s+TRUE' "LTO must remain disabled."
Assert-Contains $cmake '"VRSceneGraph\.cpp"' "The FO4VR scene-graph ABI adapter must be compiled into the plugin."
$legacySourceSet = [regex]::Match(
    $cmake,
    '(?s)set\(\s*LEGACY_GAMEPLAY_SOURCES(?<Body>.*?)\)\s*set_source_files_properties')
if (-not $legacySourceSet.Success) {
    throw "The legacy gameplay source boundary could not be located."
}
Assert-NotContains $legacySourceSet.Groups["Body"].Value 'VRSceneGraph\.cpp' "The FO4VR scene-graph adapter must compile against CommonLibF4VR, not legacy F4SE headers."

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

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHIPWRIGHT = ROOT / "source" / "shipwright"
DX12 = SHIPWRIGHT / "libultraship" / "src" / "fast" / "backends" / "gfx_direct3d12.cpp"
DXGI = SHIPWRIGHT / "libultraship" / "src" / "fast" / "backends" / "gfx_dxgi.cpp"
PIPELINE_POLICY = SHIPWRIGHT / "libultraship" / "include" / "fast" / "backends" / "DxrPipelinePolicy.h"
SEQUENCE_POLICY = SHIPWRIGHT / "libultraship" / "include" / "fast" / "backends" / "DxrSequencePolicy.h"
HARNESS = SHIPWRIGHT / "soh" / "soh" / "Enhancements" / "scene-harness" / "SceneTestHarness.cpp"
VERIFIER = SHIPWRIGHT / "scripts" / "scene-harness" / "verify-run.ps1"
RUN_SCENE = SHIPWRIGHT / "scripts" / "scene-harness" / "run-scene.ps1"
SURFACE = SHIPWRIGHT / "libultraship" / "include" / "fast" / "backends" / "DxrSurfaceRenderer.h"
MATERIALS = SHIPWRIGHT / "libultraship" / "include" / "fast" / "MaterialRegistry.h"
MATERIAL_SOURCE = SHIPWRIGHT / "libultraship" / "src" / "fast" / "MaterialRegistry.cpp"
INTERPRETER = SHIPWRIGHT / "libultraship" / "src" / "fast" / "interpreter.cpp"
DXR_SHADER = SHIPWRIGHT / "libultraship" / "src" / "fast" / "shaders" / "dxr_shadow.hlsl"
DXR_LAYOUT = SHIPWRIGHT / "libultraship" / "include" / "fast" / "backends" / "DxrAccelerationStructureManager.h"
DXR_AS_SOURCE = SHIPWRIGHT / "libultraship" / "src" / "fast" / "backends" / "DxrAccelerationStructureManager.cpp"
DXR_GENERATOR = SHIPWRIGHT / "scripts" / "Generate-DxrShadowBytecode.ps1"


class OczRtxHotfix02RegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dx12 = DX12.read_text(encoding="utf-8")
        cls.dxgi = DXGI.read_text(encoding="utf-8")
        cls.pipeline = PIPELINE_POLICY.read_text(encoding="utf-8")
        cls.sequence = SEQUENCE_POLICY.read_text(encoding="utf-8")
        cls.harness = HARNESS.read_text(encoding="utf-8")
        cls.verifier = VERIFIER.read_text(encoding="utf-8")
        cls.run_scene = RUN_SCENE.read_text(encoding="utf-8")
        cls.surface = SURFACE.read_text(encoding="utf-8")
        cls.materials = MATERIALS.read_text(encoding="utf-8")
        cls.material_source = MATERIAL_SOURCE.read_text(encoding="utf-8")
        cls.interpreter = INTERPRETER.read_text(encoding="utf-8")
        cls.dxr_shader = DXR_SHADER.read_text(encoding="utf-8")
        cls.dxr_layout = DXR_LAYOUT.read_text(encoding="utf-8")
        cls.dxr_as_source = DXR_AS_SOURCE.read_text(encoding="utf-8")
        cls.dxr_generator = DXR_GENERATOR.read_text(encoding="utf-8")

    def test_profile_policy_is_single_and_exact(self):
        self.assertIn("profileSupportsHybridRt = DxrProfileSelected(effectiveProfile)", self.dx12)
        self.assertNotIn("hybridRtProfile", self.dx12)
        self.assertIn("profile == static_cast<int>(DxrVisualProfile::HybridRt)", self.pipeline)
        self.assertIn("profile == static_cast<int>(DxrVisualProfile::PathTracedExperimental)", self.pipeline)

    def test_dispatch_requires_compatible_frame_and_valid_tlas(self):
        self.assertIn("mDxrFrameEvidence.tlasValid = tlasGpuVA != 0", self.dx12)
        self.assertIn("mDxrPassesRunThisFrame = mDxrFrameEvidence.DispatchExecuted()", self.dx12)
        self.assertNotIn("mDxrPassesRunThisFrame = true", self.dx12)
        self.assertIn("adapterCompatible && tlasValid && dispatchCalls > 0", self.pipeline)

    def test_capture_completion_validates_all_eight_and_nonfinite_signals(self):
        self.assertIn("DxrCaptureSetComplete(gHarnessCaptureEvidence)", self.dx12)
        self.assertIn('"CAPTURE_COMPLETE" : "PARTIAL"', self.dx12)
        self.assertIn("nanChannels == 0 && infChannels == 0", self.dx12)
        self.assertIn("payloadCompleteAndReadable", self.pipeline)
        self.assertIn("dxr_visual_verified", self.harness)
        self.assertIn("dxrVisualVerified", self.verifier)

    def test_capture_names_do_not_claim_raster_color_is_dxr_shaded(self):
        self.assertIn('"RASTER_SCENE_COLOR_COPY"', self.pipeline)
        self.assertIn("GetRasterSceneColorCopyForCapture", self.surface)
        self.assertNotIn("DXR_DIRECT_SHADED", self.dx12 + self.verifier)
        self.assertNotIn("GetSceneColorCopyForTrace", self.surface)

    def test_truth_modes_have_explicit_windows_and_termination(self):
        self.assertIn('"COUCH_DXR_TRUTH_SEQUENCE"', self.dx12)
        self.assertIn("DxrTruthSequenceEnabled()", self.dx12)
        self.assertIn("DxrSequenceDuration(DxrSequenceModeFromEnvironment())", self.dx12)
        self.assertIn("DxrSequenceStage(mode, elapsed)", self.dx12)
        self.assertIn("DxrSequenceCaptureDue", self.dx12)

    def test_capture_and_presentation_are_separate_observations(self):
        self.assertIn("capture_backbuffer_pre_present", self.dx12)
        self.assertIn("PRESENT_RESULT", self.dxgi)
        self.assertIn("physicalScanout=NOT_VERIFIABLE", self.dxgi)
        self.assertIn('"physical_presentation_status"', self.harness)
        self.assertIn("physicalPresentation = 'NOT_VERIFIABLE_BY_AUTOMATED_BACKBUFFER_CAPTURE'", self.verifier)

    def test_launcher_defaults_to_the_executable_the_release_build_produces(self):
        self.assertIn("$repoRoot 'x64\\Release\\soh.exe'", self.run_scene)
        self.assertIn("Build soh.exe before launching", self.run_scene)
        self.assertNotIn("soh_dxr_harness.exe", self.run_scene)

    def test_fast3d_material_state_survives_capture(self):
        self.assertIn("Fast3DMaterialStateKey", self.materials)
        self.assertIn("GetMaterialForDraw(const DrawCallInfo& info)", self.materials)
        self.assertIn("RecordDrawCall(drawInfo)", self.interpreter)
        self.assertIn("GetMaterialForDraw(drawInfo)", self.interpreter)
        self.assertIn("float captureUv[2][3][2]", self.interpreter)
        self.assertIn("textureHashes[2]", self.materials)
        self.assertIn("textureIndex1", self.dxr_layout)
        self.assertIn("combineModeLow", self.dxr_shader)
        self.assertIn("EvaluateFast3DCombiner", self.dxr_shader)

    def test_pre_lit_shade_is_only_removed_for_explicit_relight_materials(self):
        self.assertIn("requestPrimaryRelight = fast3dLitColor", self.dxr_shader)
        self.assertIn("if (combinerSupported && requestPrimaryRelight)", self.dxr_shader)
        self.assertIn("DxrPrimaryRelightEligible", self.materials)
        self.assertIn("(asset.sourceGeometryMode & G_LIGHTING)", self.dxr_as_source)
        self.assertNotIn("material.flags |= MATERIAL_FLAG_DXR_PRIMARY_RELIGHT", self.dxr_shader)
        self.assertIn("Tint and blend state alone do not prove water, lava, magic, or emission", self.material_source)

    def test_transparent_decorations_do_not_hide_the_depth_receiver(self):
        self.assertIn("camera-to-raster-depth hit", self.dxr_shader)
        self.assertIn("IgnoreHit();", self.dxr_shader)
        self.assertIn("MATERIAL_FLAG_SECOND_ALBEDO_TEXTURE_VALID", self.dxr_shader)

    def test_shader_artifact_has_abi_source_and_compiler_provenance(self):
        self.assertIn("COUCH_DXR_SHADER_ABI_VERSION 3", self.dxr_shader)
        self.assertIn("sizeof(DxrInstanceMaterialData) == 112", self.dxr_layout)
        self.assertIn("sizeof(DxrHitVertex) == 56", self.dxr_layout)
        self.assertIn("offsetof(DxrInstanceMaterialData, primitiveColor) == 96", self.dxr_layout)
        self.assertIn("kDxrShadowBytecodeDxilSha256", self.dxr_generator)
        self.assertIn("kDxrShadowBytecodeBuildId", self.dxr_generator)
        self.assertIn("--version", self.dxr_generator)
        self.assertIn("$containerMagic -ne 'DXBC'", self.dxr_generator)
        self.assertIn("-dumpbin $tempDxil", self.dxr_generator)
        self.assertIn("Compiled DXIL resource/register contract", self.dxr_generator)
        self.assertIn("C++ DXR root-signature contract", self.dxr_generator)


if __name__ == "__main__":
    unittest.main()

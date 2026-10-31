# 6.6 dump 里"角色/碰撞/相机"相关类结构速查

> 自动生成于 2026-10-31，来源 `6.6dump.cs`（190 MB 全量）。

> RVA 是 **6.6** 的值，7.1 要重新验证；字段偏移通常变化不大。


## MoleMole.MonoEntityLayerCollisionHelper
```csharp
public class MoleMole.MonoEntityLayerCollisionHelper : UnityEngine.MonoBehaviour // TypeSize: 0x20
{
	// Fields
	public static System.Nullable<System.Int32>[] collisionType2Layer;
	public static System.String[] collisionType2LayerName;
	public JNFEKCIJENF type; // Offset: 0x18

	// Constructors
	public System.Void .ctor() { } // RVA: 0x6C60EA0 VA: 0x7FF799440EA0 Token: 0x06045644
	private static System.Void .cctor() { } // RVA: 0x6C60EB0 VA: 0x7FF799440EB0 Token: 0x06045645

	// Methods
	public System.Int32 GetCollisionLayer() { } // RVA: 0x6C60F40 VA: 0x7FF799440F40 Token: 0x06045646
}

// TypeDefIndex: 52968

// Namespace:
public class LNDKMDGGIMI : BOOPHIOAEKI, HCCBLNAMAJO // TypeSize: 0x88
{
	// Fields
	private System.Collections.Generic.List<System.UInt32> FECKBPGLAMN; // Offset: 0x30
	private System.Action <OGMJBNONEHB>k__BackingField; // Offset: 0x38
	private KBAPMFBMIOB<BFMFKNKNIIA> BDJOFBHHHNM; // Offset: 0x40
	private PBDBJNLNBBK GNGPFLEMILG; // Offset: 0x50
	private GCNBPMJKOKE IPAMPDFDOLI; // Offset: 0x58
	private readonly GABAGGONIDD <NHBJMJLILOI>k__BackingField; // Offset: 0x60
	private ALHBKDIKFID OCGKPMHHCFO; // Offset: 0x68
	private HLOOLCGNLGL CGGLNDPEBLJ; // Offset: 0x70
	private System.Boolean DPKGGBLOBAP; // Offset: 0x78
	private System.Boolean BMPELCKNDEC; // Offset: 0x79
	private System.Boolean DCOOLKKMNLP; // Offset: 0x7A
	private AJCOLHNMOKN <NBNLMAEJLEP>k__BackingField; // Offset: 0x7C
	private BKEPPJNICFE BBPMGOJAHCP; // Offset: 0x80
```

## MoleMole.MonoWallCollisionHelper
```csharp
public class MoleMole.MonoWallCollisionHelper : UnityEngine.MonoBehaviour // TypeSize: 0x48
{
	// Fields
	public System.Action<UnityEngine.Collision, UnityEngine.GameObject> collisionStayCallback; // Offset: 0x18
	public System.Action<UnityEngine.Collision, UnityEngine.GameObject> collisionEnterCallback; // Offset: 0x20
	public System.Action<UnityEngine.Collision, UnityEngine.GameObject> collisionExitCallback; // Offset: 0x28
	public System.Action<UnityEngine.Collider, UnityEngine.GameObject> triggerEnterCallback; // Offset: 0x30
	public System.Action<UnityEngine.Collider, UnityEngine.GameObject> triggerStayCallback; // Offset: 0x38
	public System.Action<UnityEngine.Collider, UnityEngine.GameObject> triggerExitCallback; // Offset: 0x40

	// Constructors
	public System.Void .ctor() { } // RVA: 0x6CB4390 VA: 0x7FF799494390 Token: 0x0602EFFC

	// Methods
	public System.Void OnTriggerExit(UnityEngine.Collider PEAHALMGNDL) { } // RVA: 0x6CB4090 VA: 0x7FF799494090 Token: 0x0602EFFD
	public System.Void OnCollisionExit(UnityEngine.Collision PIPKGLPLHDK) { } // RVA: 0x6CB4110 VA: 0x7FF799494110 Token: 0x0602EFFE
	public System.Void OnCollisionEnter(UnityEngine.Collision PIPKGLPLHDK) { } // RVA: 0x6CB4190 VA: 0x7FF799494190 Token: 0x0602EFFF
	public System.Void OnTriggerStay(UnityEngine.Collider PEAHALMGNDL) { } // RVA: 0x6CB4210 VA: 0x7FF799494210 Token: 0x0602F000
	public System.Void OnTriggerEnter(UnityEngine.Collider PEAHALMGNDL) { } // RVA: 0x6CB4290 VA: 0x7FF799494290 Token: 0x0602F001
	public System.Void OnCollisionStay(UnityEngine.Collision PIPKGLPLHDK) { } // RVA: 0x6CB4310 VA: 0x7FF799494310 Token: 0x0602F002
}

// TypeDefIndex: 42779

// Namespace: MoleMole
public class MoleMole.MonoAmbientAudioPlaceholder : UnityEngine.MonoBehaviour, OLBFBNOAGEM // TypeSize: 0x38
{
	// Fields
	private System.String _ambientAudioPrefabPath; // Offset: 0x18
	private System.UInt64 OLJBNJGOIGE; // Offset: 0x20
	private UnityEngine.Vector3 GHCHBLPKLLK; // Offset: 0x28

	// Properties
	public System.String ambientAudioPrefabPath { get; set; }
```

## 所有名字含 Collision 的类

- `CollisionBehavior`
- `MoleMole.MonoBirdPinballCollision`
- `MoleMole.MonoCollisionHelper`
- `MoleMole.MonoCollisionHelperAdv`
- `MoleMole.MonoEntityLayerCollisionHelper`
- `MoleMole.MonoGcgDiceCollision`
- `MoleMole.MonoWallCollisionHelper`
- `MonoAudioTriggerCollisionEnter`
- `MonoAudioTriggerCollisionExit`
- `RootMotion.Dynamics.MuscleCollisionBroadcaster`
- `UnityEngine.Collision`
- `UnityEngine.Collision2D`
- `XLua.CSObjectWrap.MoleMoleILuaActorCollisionInfoBridge`

## 全库布尔型碰撞/命中字段（可能藏着碰撞开关）

| 类 | 字段 | 偏移 |
|---|---|---|
| MasterProps | activateOnStaticCollisions | 0x18 |
| RootMotion.Dynamics.PuppetMasterHumanoidConfig | internalCollisions | 0x63 |
| RootMotion.Dynamics.PuppetMasterSettings | collisionStayMessages | 0x30 |
| RootMotion.Dynamics.PuppetMasterSettings | collisionExitMessages | 0x31 |
| HedgehogTeam.EasyTouch.QuickDrag | isStopOncollisionEnter | 0x88 |
| VLB_EX.MonoLightBeamOcclusion | usePlaneInterpolation | 0xDC |
| MoleMole.Activity.DreamSwitching.DSAttachComponentConfig | disableCollisionWhenAttach | 0x1C |
| MoleMole.Activity.DreamSwitching.DSPlaneEntityInfo | ignoreSideCollision | 0x58 |
| MoleMole.LocalLightClipPlaneOperations | updateFromClipPlanes | 0x30 |
| MoleMole.MonoHomeworldPlaceCol | useAllColliderForVerify | 0x28 |
| MoleMole.MonoBirdPinballHittable | disableCollisionWhenDying | 0x24 |
| MoleMole.MonoBirdPinballHittable | canFireHitEffect | 0x30 |
| MoleMole.RelativeLocalLightClipPlaneOperations | updateFromClipPlanes | 0x30 |
| MoleMole.MonoPatternSceneProp | chairIgnoreDynamicColliderCheck | 0x54 |
| MoleMole.Activity.DreamSwitching.DSEntityInfoBase | editCollider | 0x3C |
| VerletEngine.StaticHorizonLine | collision | 0x30 |
| MoleMole.BeyondCameraSettingStyle | ignoreCollision | 0x5C |
| MoleMole.MonoEffectPluginLineRenderer | replaceEndByHitPoint | 0x2B4 |
| MoleMole.MonoEffectPluginLineParabola | trimEndByHitPoint | 0x2B8 |
| MoleMole.MonoCameraFieldHelper | enableCameraCollision | 0x4D |

## 相机相关类

- `miHoYoCamera.CameraStack`
- `miHoYoCamera.CameraStateBlenderConfig`
- `miHoYoCamera.CameraBlendState`
- `miHoYoCamera.CameraStateBlender`
- `miHoYoCamera.BaseCameraState`
- `miHoYoCamera.CameraStateMgr`
- `miHoYoCamera.CameraStatePostModule`
- `miHoYoCamera.CameraStatePostProcesser`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.BaseCameraStateRunner`
- `miHoYoCamera.Example.ThreadCameraBlenderTask`
- `CameraStateGroup`
- `CameraState`
- `CameraState`
- `CameraStateGroup`
- `CameraStateGroup`
- `CameraState`
- `CameraStateGroup`
- `CameraState`
- `CameraStateGroup`
- `CameraState`
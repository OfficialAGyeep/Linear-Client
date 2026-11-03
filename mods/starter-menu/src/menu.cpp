// Linear Client (based on the starter menu): a hand menu in the style of ii's Stupid Menu (Gorilla Tag).
// The layout idea comes from ii's menu; the code is new.
//   Hold Y (left hand)      -> menu on your left hand, tipped like ii's; turn your palm to you
//                              (B and the right hand with "Right Hand Menu")
//   Touch a button with your other index finger to press it
//   < and > on the sides    -> previous / next page
//   Pages: Movement Mods, Rig Mods, Items, Settings, Enabled Mods
// Movement only changes your own game. Rig mods (dances) and Items are seen by the room,
// within the limits in AGENTS.md ("Things others see"). Read docs/GUIDE.md before adding.
#include "ymsdk.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using namespace BNM;
using namespace BNM::Structures::Unity;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("Linear Client")

// ---- small helpers ---------------------------------------------------------

static std::string TypeName(const BNM::IL2CPP::Il2CppType *t) {
    Class c(t);
    if (!c.IsValid() || !c.GetClass() || !c.GetClass()->name) return "?";
    auto *k = c.GetClass();
    return (k->namespaze && k->namespaze[0]) ? std::string(k->namespaze) + "." + k->name : k->name;
}
// Pick an overload by its exact parameter types, e.g. GetComponent(System.Type).
static MethodBase Overload(Class c, const char *name, std::vector<std::string> types) {
    for (auto &m : c.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || strcmp(mi->name, name) != 0 || mi->parameters_count != types.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < types.size() && ok; i++) ok = TypeName(mi->parameters[i]) == types[i];
        if (ok) return m;
    }
    return {};
}

// Unity objects we keep between frames must be held by a GC handle, or the
// garbage collector can free them under us.
static void *(*p_handleNew)(void *, bool);
static void *(*p_handleTarget)(void *);
static void (*p_handleFree)(void *);
struct Ref {
    void *h = nullptr;
    void set(Il2CppObject *o) { clear(); if (o && p_handleNew) h = p_handleNew(o, false); }
    Il2CppObject *get() const { return h && p_handleTarget ? (Il2CppObject *)p_handleTarget(h) : nullptr; }
    void clear() { if (h && p_handleFree) p_handleFree(h); h = nullptr; }
};

static Color Lerp(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
}
static Color Rainbow(float h) {             // hue 0..1 -> bright color
    h = h - floorf(h);
    float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
    auto c = [](float v) { return v < 0 ? 0.f : v > 1 ? 1.f : v; };
    return Color(c(r), c(g), c(b), 1);
}

// ---- game lookups, resolved once -------------------------------------------

struct XRDevice { uint64_t id; bool initialized; };
static struct {
    bool ok = false;
    Method<Il2CppObject *> createPrimitive, goTransform, goGetComponent, goAddComponent, compTransform, compGameObject;
    Method<void> goSetActive, destroy, trSetParent, trSetPosition, trSetRotation, trSetLocalPosition,
                 trSetLocalRotation, trSetLocalScale, rbSetVelocity, matSetColor, matSetShader,
                 textSetFont, textSetText, textSetFontSize, textSetAlignment, graphicSetColor,
                 canvasSetRenderMode, rtSetSizeDelta, textSetFontStyle, textSetBestFit, textSetMinSize,
                 textSetMaxSize, teleportTo, rbAddForce, rbSetUseGravity, colliderSetEnabled,
                 lrSetPositionCount, lrSetPosition, lrSetStartWidth, lrSetEndWidth, lrSetUseWorldSpace;
    Method<Quaternion> trGetRotation;
    Method<Color> matGetColor;
    Method<Vector3> trGetPosition, trGetForward, trGetRight, trGetUp, trInverseTransformPoint, trTransformPoint, rbGetVelocity;
    Method<bool> alive, tryGetBool, haptic, raycast;
    Method<XRDevice> deviceAtNode;
    Method<Il2CppObject *> trGetParent, trGetRoot, getComponentsInChildren, netLocal;
    Method<void> pvRpc;
    BNM::IL2CPP::MethodInfo *rpcCreateItem = nullptr;     // only read for its parameter types
    BNM::IL2CPP::Il2CppClass *objectClass = nullptr, *stringClass = nullptr;
    BNM::MonoType *tPhotonView = nullptr, *tItem = nullptr;
    Method<BNM::Structures::Mono::String *> itemKey, itemId;
    Method<bool> itemHeld;
    Method<void> itemDrop, setHeldItem;
    void *(*arrayNew)(void *, uintptr_t) = nullptr;
    void *(*valueBox)(void *, void *) = nullptr;
    Method<bool> behaviourGetEnabled;
    Method<void> behaviourSetEnabled;
    std::vector<BNM::MonoType *> trackerTypes;
    Method<Il2CppObject *> rendererGetMaterial, shaderFind, findAll, findScene, findSceneByType, builtinResource;
    Field<Il2CppObject *> leftHand, rightHand, headTransform, bodyCollider, headCollider;
    Field<float> maxJumpSpeed, jumpMultiplier;
    Method<Vector3> plGetVelocity;
    Method<void> plSetVelocity, plNoGravityFor;
    Field<int> locomotionLayers, collisionLayers;
    Method<int> goGetLayer;
    Method<bool> colliderIsTrigger, behaviourGetEnabledCollider;
    Method<void> ignoreLayers;            // a LayerMask: which layers your hands can climb on
    Method<void> goSetLayer;
    BNM::MonoType *tRigidbody = nullptr, *tCollider = nullptr, *tCanvas = nullptr, *tText = nullptr,
                  *tFont = nullptr, *tRenderer = nullptr, *tMeshCollider = nullptr, *tLine = nullptr;
    Ref shader, font, usagePrimary, usageSecondary, usageGrip, usageTrigger;
} G;

static bool Lookups() {
    if (G.ok) return true;
    auto R = ym::api()->resolve;
    p_handleNew = (decltype(p_handleNew))R("il2cpp_gchandle_new");
    p_handleTarget = (decltype(p_handleTarget))R("il2cpp_gchandle_get_target");
    p_handleFree = (decltype(p_handleFree))R("il2cpp_gchandle_free");
    Class go("UnityEngine", "GameObject"), tr("UnityEngine", "Transform"), obj("UnityEngine", "Object"),
          rb("UnityEngine", "Rigidbody"), mat("UnityEngine", "Material"), rend("UnityEngine", "Renderer"),
          sh("UnityEngine", "Shader"), res("UnityEngine", "Resources"), text("UnityEngine.UI", "Text"),
          graphic("UnityEngine.UI", "Graphic"), canvas("UnityEngine", "Canvas"), rt("UnityEngine", "RectTransform"),
          dev("UnityEngine.XR", "InputDevices"), player("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!go || !tr || !player || !dev) { ym::log("linear-client: game classes missing"); return false; }
    G.createPrimitive = go.GetMethod("CreatePrimitive", 1);
    G.goTransform = go.GetMethod("get_transform", 0);
    G.goGetComponent = Overload(go, "GetComponent", {"System.Type"});
    G.goAddComponent = Overload(go, "AddComponent", {"System.Type"});
    G.goSetActive = go.GetMethod("SetActive", 1);
    Class comp("UnityEngine", "Component");
    G.compTransform = comp.GetMethod("get_transform", 0);
    G.compGameObject = comp.GetMethod("get_gameObject", 0);
    G.destroy = Overload(obj, "Destroy", {"UnityEngine.Object"});
    G.alive = obj.GetMethod("op_Implicit", 1);
    G.trSetParent = Overload(tr, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    G.trSetPosition = tr.GetMethod("set_position", 1);
    G.trSetRotation = tr.GetMethod("set_rotation", 1);
    G.trSetLocalPosition = tr.GetMethod("set_localPosition", 1);
    G.trSetLocalRotation = tr.GetMethod("set_localRotation", 1);
    G.trSetLocalScale = tr.GetMethod("set_localScale", 1);
    G.trGetPosition = tr.GetMethod("get_position", 0);
    G.trGetRotation = tr.GetMethod("get_rotation", 0);
    G.trGetForward = tr.GetMethod("get_forward", 0);
    G.trGetRight = tr.GetMethod("get_right", 0);
    G.trGetUp = tr.GetMethod("get_up", 0);
    G.trInverseTransformPoint = Overload(tr, "InverseTransformPoint", {"UnityEngine.Vector3"});
    G.trTransformPoint = Overload(tr, "TransformPoint", {"UnityEngine.Vector3"});
    G.rbSetVelocity = rb.GetMethod("set_velocity", 1);
    G.rbGetVelocity = rb.GetMethod("get_velocity", 0);
    G.rendererGetMaterial = rend.GetMethod("get_material", 0);
    G.matSetColor = Overload(mat, "set_color", {"UnityEngine.Color"});
    G.matGetColor = mat.GetMethod("get_color", 0);
    G.matSetShader = mat.GetMethod("set_shader", 1);
    G.shaderFind = sh.GetMethod("Find", 1);
    G.findAll = Overload(res, "FindObjectsOfTypeAll", {"System.Type"});
    G.textSetFont = text.GetMethod("set_font", 1);
    G.textSetFontStyle = text.GetMethod("set_fontStyle", 1);
    G.textSetBestFit = text.GetMethod("set_resizeTextForBestFit", 1);
    G.textSetMinSize = text.GetMethod("set_resizeTextMinSize", 1);
    G.textSetMaxSize = text.GetMethod("set_resizeTextMaxSize", 1);
    G.textSetText = text.GetMethod("set_text", 1);
    G.textSetFontSize = text.GetMethod("set_fontSize", 1);
    G.textSetAlignment = text.GetMethod("set_alignment", 1);
    G.graphicSetColor = graphic.GetMethod("set_color", 1);
    G.canvasSetRenderMode = canvas.GetMethod("set_renderMode", 1);
    G.rtSetSizeDelta = rt.GetMethod("set_sizeDelta", 1);
    G.deviceAtNode = dev.GetMethod("GetDeviceAtXRNode", 1);
    G.tryGetBool = dev.GetMethod("TryGetFeatureValue_bool", 3);
    G.haptic = dev.GetMethod("SendHapticImpulse", 4);    // optional: a small buzz on press
    G.leftHand = player.GetField("leftHandTransform");
    G.rightHand = player.GetField("rightHandTransform");
    G.headTransform = player.GetField("headTransform");
    G.bodyCollider = player.GetField("bodyCollider");
    G.headCollider = player.GetField("headCollider");
    G.maxJumpSpeed = player.GetField("maxJumpSpeed");
    G.locomotionLayers = player.GetField("locomotionEnabledLayers");
    G.collisionLayers = player.GetField("collisionEnabledLayers");
    G.goGetLayer = go.GetMethod("get_layer", 0);
    G.ignoreLayers = Class("UnityEngine", "Physics").GetMethod("IgnoreLayerCollision", 3);
    // Yeeps moves your body through these, not through a Rigidbody on the player object
    G.plGetVelocity = player.GetMethod("GetVelocity", 0);
    G.plSetVelocity = player.GetMethod("ApplySetVelocityNoHaptic", 1);
    if (!G.plSetVelocity.IsValid()) G.plSetVelocity = player.GetMethod("ApplySetVelocity", 1);
    G.plNoGravityFor = player.GetMethod("ApplyNoGravityDuration", 1);
    ym::log("linear-client: velocity %d/%d, no-gravity %d", G.plGetVelocity.IsValid(), G.plSetVelocity.IsValid(),
            G.plNoGravityFor.IsValid());
    G.goSetLayer = go.GetMethod("set_layer", 1);
    G.jumpMultiplier = player.GetField("jumpMultiplier");
    G.teleportTo = player.GetMethod("TeleportTo", 1);
    G.rbAddForce = Overload(rb, "AddForce", {"UnityEngine.Vector3", "UnityEngine.ForceMode"});
    G.rbSetUseGravity = rb.GetMethod("set_useGravity", 1);
    G.colliderSetEnabled = Class("UnityEngine", "Collider").GetMethod("set_enabled", 1);
    G.behaviourGetEnabledCollider = Class("UnityEngine", "Collider").GetMethod("get_enabled", 0);
    G.colliderIsTrigger = Class("UnityEngine", "Collider").GetMethod("get_isTrigger", 0);
    G.raycast = Overload(Class("UnityEngine", "Physics"), "Raycast",
                         {"UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.RaycastHit", "System.Single", "System.Int32"});
    G.findScene = Overload(obj, "FindObjectsOfType", {"System.Type"});      // only when noclip starts
    G.findSceneByType = Overload(obj, "FindObjectsByType", {"System.Type", "UnityEngine.FindObjectsSortMode"});
    G.builtinResource = Overload(res, "GetBuiltinResource", {"System.Type", "System.String"});
    Class line("UnityEngine", "LineRenderer");
    G.tLine = line.GetMonoType();
    G.lrSetPositionCount = line.GetMethod("set_positionCount", 1);
    G.lrSetPosition = line.GetMethod("SetPosition", 2);
    G.lrSetStartWidth = line.GetMethod("set_startWidth", 1);
    G.lrSetEndWidth = line.GetMethod("set_endWidth", 1);
    G.lrSetUseWorldSpace = line.GetMethod("set_useWorldSpace", 1);
    G.tMeshCollider = Class("UnityEngine", "MeshCollider").GetMonoType();
    G.trGetParent = tr.GetMethod("get_parent", 0);
    G.trGetRoot = tr.GetMethod("get_root", 0);
    G.getComponentsInChildren = Overload(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    Class net("", "NetworkMessenger", Image("Assembly-CSharp.dll"));
    if (net.IsValid()) {
        G.netLocal = net.GetMethod("get_local", 0);
        for (auto &m : net.GetMethods(false))
            if (m.GetInfo() && m.GetInfo()->name && strcmp(m.GetInfo()->name, "RpcCreateItem") == 0) G.rpcCreateItem = m.GetInfo();
    }
    {
        Class pv("Photon.Pun", "PhotonView");
        G.tPhotonView = pv.IsValid() ? pv.GetMonoType() : nullptr;
        if (pv.IsValid()) G.pvRpc = Overload(pv, "RPC", {"System.String", "Photon.Pun.RpcTarget", "System.Object[]"});
        Class item("", "Item", Image("Assembly-CSharp.dll"));
        if (item.IsValid()) {
            G.tItem = item.GetMonoType();
            G.itemKey = item.GetMethod("get_itemKey", 0);
            G.itemHeld = item.GetMethod("IsHeldOrAnchoredToAnyPlayer", 0);
            G.itemId = item.GetMethod("get_itemID", 0);
            G.itemDrop = item.GetMethod("TrySendDrop", 0);
            G.setHeldItem = Class("", "MasterPlayer", Image("Assembly-CSharp.dll")).GetMethod("SetHeldItem", 2);
        }
        G.objectClass = Class("System", "Object").GetClass();
        G.stringClass = Class("System", "String").GetClass();
        G.arrayNew = (decltype(G.arrayNew))ym::api()->resolve("il2cpp_array_new");
        G.valueBox = (decltype(G.valueBox))ym::api()->resolve("il2cpp_value_box");
        ym::log("linear-client: items rpc=%d pv=%d item=%d box=%d", G.rpcCreateItem != nullptr, G.pvRpc.IsValid(),
                G.itemKey.IsValid(), G.arrayNew && G.valueBox);
    }
    Class behaviour("UnityEngine", "Behaviour");
    G.behaviourGetEnabled = behaviour.GetMethod("get_enabled", 0);
    G.behaviourSetEnabled = behaviour.GetMethod("set_enabled", 1);
    // What moves your hands with the controllers (paused while you dance)
    for (auto nc : std::vector<std::pair<const char *, const char *>>{
             {"UnityEngine.XR.Interaction.Toolkit", "XRBaseController"},
             {"UnityEngine.InputSystem.XR", "TrackedPoseDriver"},
             {"UnityEngine.SpatialTracking", "TrackedPoseDriver"}}) {
        Class c(nc.first, nc.second);
        if (c.IsValid()) G.trackerTypes.push_back(c.GetMonoType());
    }
    G.tRigidbody = rb.GetMonoType();
    G.tCollider = Class("UnityEngine", "Collider").GetMonoType();
    G.tRenderer = rend.GetMonoType();
    G.tCanvas = canvas.GetMonoType();
    G.tText = text.GetMonoType();
    G.tFont = Class("UnityEngine", "Font").GetMonoType();
    G.usagePrimary.set((Il2CppObject *)CreateMonoString("PrimaryButton"));
    G.usageSecondary.set((Il2CppObject *)CreateMonoString("SecondaryButton"));
    G.usageGrip.set((Il2CppObject *)CreateMonoString("GripButton"));
    G.usageTrigger.set((Il2CppObject *)CreateMonoString("TriggerButton"));
    for (const char *s : {"Universal Render Pipeline/Unlit", "Universal Render Pipeline/Lit"})
        if (!G.shader.get() && G.shaderFind.IsValid()) G.shader.set(G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(s)));
    // Arial, like ii's menu (Unity 6 calls it LegacyRuntime.ttf)
    for (const char *f : {"LegacyRuntime.ttf", "Arial.ttf"})
        if (!G.font.get() && G.builtinResource.IsValid() && G.tFont)
            G.font.set(G.builtinResource((Il2CppObject *)G.tFont, CreateMonoString(f)));
    if (!G.font.get() && G.findAll.IsValid() && G.tFont) {   // else any font the game loaded (one time)
        auto *arr = (uint8_t *)G.findAll((Il2CppObject *)G.tFont);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) G.font.set(((Il2CppObject **)(arr + 0x20))[0]);
    }
    G.ok = G.createPrimitive.IsValid() && G.goTransform.IsValid() && G.trSetPosition.IsValid() &&
           G.deviceAtNode.IsValid() && G.tryGetBool.IsValid() && G.leftHand.IsValid();
    if (!G.ok) ym::log("linear-client: some game methods are missing, menu off");
    return G.ok;
}

static bool Alive(Il2CppObject *o) { return o && G.alive((Il2CppObject *)o); }
static Il2CppObject *Tr(Il2CppObject *gameObject) { return G.goTransform[gameObject](); }
static void Show(Ref &r, bool on) { if (Alive(r.get())) G.goSetActive[r.get()](on); }
static void Kill(Ref &r) { if (Alive(r.get())) G.destroy(r.get()); r.clear(); }

// A primitive with a flat color. collider=false removes its collider so it can't push you.
// Always give new objects the URP shader: Unity's default material only draws in one
// eye on Quest.
static Il2CppObject *Prim(int type, Color c, bool collider) {
    Il2CppObject *o = G.createPrimitive(type);   // 0 = Sphere, 3 = Cube
    if (!o) return nullptr;
    if (!collider) if (auto *col = G.goGetComponent[o](G.tCollider)) G.destroy(col);
    if (auto *r = G.goGetComponent[o](G.tRenderer)) {
        Il2CppObject *m = G.rendererGetMaterial[r]();
        if (m && G.shader.get()) G.matSetShader[m](G.shader.get());
        if (m) G.matSetColor[m](c);
    }
    return o;
}
static Il2CppObject *Cube(Color c, bool collider) { return Prim(3, c, collider); }
static void SetColor(Il2CppObject *o, Color c) {
    if (!Alive(o)) return;
    if (auto *r = G.goGetComponent[o](G.tRenderer)) if (Il2CppObject *m = G.rendererGetMaterial[r]()) G.matSetColor[m](c);
}

// A rounded rectangle made of shapes: two cubes in a cross plus a cylinder in each corner
// (a pill when the radius is half the short side). One root object holds the parts, and
// their materials are kept so a recolor is one call per part.
struct RRect { Ref root, mat[6]; int n = 0; };
static void RRClear(RRect &r) { r.root.clear(); for (auto &m : r.mat) m.clear(); r.n = 0; }
static void RRShow(RRect &r, bool on) { Show(r.root, on); }
static void RRColor(RRect &r, Color c) {
    for (int i = 0; i < r.n; i++)
        if (Il2CppObject *m = r.mat[i].get()) if (Alive(m)) G.matSetColor[m](c);
}
static void RRPart(RRect &rr, Il2CppObject *parentT, int type, Color c, Vector3 pos, Vector3 scale, bool cylinder) {
    Il2CppObject *o = Prim(type, c, false);
    if (!o) return;
    Il2CppObject *t = Tr(o);
    G.trSetParent[t](parentT, false);
    G.trSetLocalPosition[t](pos);
    if (cylinder) G.trSetLocalRotation[t](Quaternion(0.70710678f, 0, 0, 0.70710678f));   // its axis along z
    G.trSetLocalScale[t](scale);
    if (rr.n < 6)
        if (auto *rend = G.goGetComponent[o](G.tRenderer)) if (Il2CppObject *m = G.rendererGetMaterial[rend]()) rr.mat[rr.n++].set(m);
}
static void RRBuild(RRect &rr, Il2CppObject *parentT, Color c, Vector3 center, float w, float h, float th, float rad) {
    RRClear(rr);
    static Class goClass("UnityEngine", "GameObject");
    Il2CppObject *root = goClass.CreateNewObjectParameters(CreateMonoString("RRect"));
    if (!root) return;
    Il2CppObject *rt = Tr(root);
    G.trSetParent[rt](parentT, false);
    G.trSetLocalPosition[rt](center);
    rr.root.set(root);
    const float eps = 1e-4f;
    float r = fminf(rad, fminf(w, h) / 2);
    float ax = w / 2 - r, ay = h / 2 - r;                // where the corner circles sit
    float tc = th * 0.985f;                              // a hair thinner: no flicker where shapes overlap
    if (ax > eps) RRPart(rr, rt, 3, c, Vector3(0, 0, 0), Vector3(w - 2 * r, h, th), false);
    if (ay > eps) RRPart(rr, rt, 3, c, Vector3(0, 0, 0), Vector3(w, h - 2 * r, ax > eps ? tc : th), false);
    float xs[2] = {-ax, ax}, ys[2] = {-ay, ay};
    int nx = ax > eps ? 2 : 1, ny = ay > eps ? 2 : 1;
    if (nx == 1) xs[0] = 0;
    if (ny == 1) ys[0] = 0;
    for (int i = 0; i < nx; i++)
        for (int j = 0; j < ny; j++)
            RRPart(rr, rt, 2, c, Vector3(xs[i], ys[j], 0), Vector3(2 * r, tc / 2, 2 * r), true);
}
static void SetText(Ref &text, const char *s) {
    if (Alive(text.get())) G.textSetText[text.get()](CreateMonoString(s));
}

// ---- input -------------------------------------------------------------------

struct Buttons { bool a = false, b = false, x = false, y = false, lgrip = false, rgrip = false, ltrig = false, rtrig = false; };
static bool Pressed(int node, Ref &usage) {
    XRDevice d = G.deviceAtNode(node);
    bool v = false;
    if (d.initialized) G.tryGetBool(d.id, (BNM::Structures::Mono::String *)usage.get(), &v);
    return v;
}
static Buttons ReadButtons() {
    const int left = 4, right = 5;              // XRNode.LeftHand / RightHand
    Buttons b;
    b.x = Pressed(left, G.usagePrimary);  b.y = Pressed(left, G.usageSecondary);
    b.a = Pressed(right, G.usagePrimary); b.b = Pressed(right, G.usageSecondary);
    b.lgrip = Pressed(left, G.usageGrip); b.rgrip = Pressed(right, G.usageGrip);
    b.ltrig = Pressed(left, G.usageTrigger); b.rtrig = Pressed(right, G.usageTrigger);
    return b;
}

// ---- menu contents -------------------------------------------------------------

// Feature switches. Every feature only changes your own game.
static bool f_velocityFly, f_ironMan, f_tpGun, f_noclip, f_speedBoost, f_noGravity;
static bool f_itemGun, f_spawnOnHand, f_car;
static char g_itemKey[64] = "";                // the last item you held (learned on grab)
struct PendingItem { Vector3 pos, vel; };
static std::vector<PendingItem> g_itemQueue;
static double g_nextItem = 0, g_nextBatch = 0;
static int g_dupeArmed = 0;

static constexpr int kDances = 8;
static bool g_danceSel[kDances];
static int g_dance = -1;            // the picked dance, -1 = none
static bool f_fly, f_fastFly, f_platforms, f_lowGravity, f_rainbow, f_fps, f_clock, f_rightHand,
            f_notify = true;
static bool f_airBrake, f_jetpack, f_speed, f_pos, f_noFog, f_draw, f_longArms, f_hud;  // Linear Client additions
static Ref g_player, g_rigidbody, g_leftHand, g_rightHand, g_head;
static Vector3 g_spot;
static bool g_haveSpot = false;

static void Notify(const char *fmt, ...);
static void NextSkinColor();
static void NextEyeColor();
static void QuitGame();
static void NextDrawColor();
static void NextDrawSize();
static void UndoDrawing();
static void PlaceCube();
static void PlaceSphere();
static void ClearObjects();
static float Rand01();
static bool GetVel(Vector3 &v);
static bool BodyPos(Vector3 &out);
static void SaveSpot();
static void GoToSpot();
static void NextTheme();
static void AllOff();
static void PointerToHand();
static void NextPointer();
static void NextIronSpeed();
static void NextCarSpeed();
static void ApplySpeedBoost();
static void ApplyGravity();
static void DancePicked();
static void ArmDupe9();
static void ArmDupe27();
static bool LeftHandItem();

enum Cat { MAIN, MOVEMENT, RIG, ITEMS, OBJECTS, SETTINGS, ENABLED, NCAT };
static const char *kCatName[NCAT] = {"Main", "Movement Mods", "Rig Mods", "Items", "Objects", "Settings", "Enabled Mods"};
enum Kind { OPEN, TOGGLE, ACTION };
struct Item {
    Cat cat;
    const char *label;
    Kind kind;
    Cat target = MAIN;            // OPEN: the page it opens
    bool *flag = nullptr;         // TOGGLE: the switch
    void (*fn)() = nullptr;       // ACTION, or called after a TOGGLE changes
};
// The owner's layout (2026-10-31). Keep this order.
static Item g_items[] = {
    {MAIN, "Settings", OPEN, SETTINGS},
    {MAIN, "Movement Mods", OPEN, MOVEMENT},
    {MAIN, "Rig Mods", OPEN, RIG},
    {MAIN, "Items", OPEN, ITEMS},
    {MAIN, "Objects", OPEN, OBJECTS},
    {MAIN, "Enabled Mods", OPEN, ENABLED},

    {MOVEMENT, "Exit Movement Mods", OPEN, MAIN},
    {MOVEMENT, "Platforms", TOGGLE, MAIN, &f_platforms},
    {MOVEMENT, "Fly", TOGGLE, MAIN, &f_fly},
    {MOVEMENT, "Fast Fly", TOGGLE, MAIN, &f_fastFly},
    {MOVEMENT, "Velocity Fly", TOGGLE, MAIN, &f_velocityFly},
    {MOVEMENT, "Car", TOGGLE, MAIN, &f_car},
    {MOVEMENT, "Teleport Gun", TOGGLE, MAIN, &f_tpGun},
    {MOVEMENT, "Iron Man", TOGGLE, MAIN, &f_ironMan},
    {MOVEMENT, "Noclip", TOGGLE, MAIN, &f_noclip},
    {MOVEMENT, "Speed Boost", TOGGLE, MAIN, &f_speedBoost, ApplySpeedBoost},
    {MOVEMENT, "No Gravity", TOGGLE, MAIN, &f_noGravity, ApplyGravity},
    {MOVEMENT, "Low Gravity", TOGGLE, MAIN, &f_lowGravity},
    {MOVEMENT, "Air Brake", TOGGLE, MAIN, &f_airBrake},
    {MOVEMENT, "Jetpack", TOGGLE, MAIN, &f_jetpack},
    {MOVEMENT, "Long Arms", TOGGLE, MAIN, &f_longArms},
    {MOVEMENT, "Save Spot", ACTION, MAIN, nullptr, SaveSpot},
    {MOVEMENT, "Go To Spot", ACTION, MAIN, nullptr, GoToSpot},


    {RIG, "Exit Rig Mods", OPEN, MAIN},
    {RIG, "Wave", TOGGLE, MAIN, &g_danceSel[0], DancePicked},
    {RIG, "Helicopter", TOGGLE, MAIN, &g_danceSel[1], DancePicked},
    {RIG, "T-Pose", TOGGLE, MAIN, &g_danceSel[2], DancePicked},
    {RIG, "Windmill", TOGGLE, MAIN, &g_danceSel[3], DancePicked},
    {RIG, "Swim", TOGGLE, MAIN, &g_danceSel[4], DancePicked},
    {RIG, "Jumping Jacks", TOGGLE, MAIN, &g_danceSel[5], DancePicked},
    {RIG, "Spaz", TOGGLE, MAIN, &g_danceSel[6], DancePicked},
    {RIG, "Beyblade", TOGGLE, MAIN, &g_danceSel[7], DancePicked},
    {RIG, "Skin Color", ACTION, MAIN, nullptr, NextSkinColor},
    {RIG, "Eye Color", ACTION, MAIN, nullptr, NextEyeColor},

    {ITEMS, "Exit Items", OPEN, MAIN},
    {ITEMS, "Spawn Item Gun", TOGGLE, MAIN, &f_itemGun},
    {ITEMS, "Duplicate Items (3x3)", ACTION, MAIN, nullptr, ArmDupe9},
    {ITEMS, "Duplicate Items (3x3x3)", ACTION, MAIN, nullptr, ArmDupe27},
    {ITEMS, "Spawn On Hand", TOGGLE, MAIN, &f_spawnOnHand},

    {OBJECTS, "Exit Objects", OPEN, MAIN},
    {OBJECTS, "Draw", TOGGLE, MAIN, &f_draw},
    {OBJECTS, "Draw Color", ACTION, MAIN, nullptr, NextDrawColor},
    {OBJECTS, "Draw Size", ACTION, MAIN, nullptr, NextDrawSize},
    {OBJECTS, "Undo Drawing", ACTION, MAIN, nullptr, UndoDrawing},
    {OBJECTS, "Place Cube", ACTION, MAIN, nullptr, PlaceCube},
    {OBJECTS, "Place Sphere", ACTION, MAIN, nullptr, PlaceSphere},
    {OBJECTS, "Clear Objects", ACTION, MAIN, nullptr, ClearObjects},

    {SETTINGS, "Exit Settings", OPEN, MAIN},
    {SETTINGS, "Change Pointer Position", ACTION, MAIN, nullptr, NextPointer},
    {SETTINGS, "Iron Man Speed", ACTION, MAIN, nullptr, NextIronSpeed},
    {SETTINGS, "Car Speed", ACTION, MAIN, nullptr, NextCarSpeed},
    {SETTINGS, "Right Hand Menu", TOGGLE, MAIN, &f_rightHand, PointerToHand},
    {SETTINGS, "Change Menu Theme", ACTION, MAIN, nullptr, NextTheme},
    {SETTINGS, "Rainbow Menu", TOGGLE, MAIN, &f_rainbow},
    {SETTINGS, "Show FPS", TOGGLE, MAIN, &f_fps},
    {SETTINGS, "Show Clock", TOGGLE, MAIN, &f_clock},
    {SETTINGS, "FPS/Clock On Screen", TOGGLE, MAIN, &f_hud},
    {SETTINGS, "Notifications", TOGGLE, MAIN, &f_notify},
    {SETTINGS, "Speedometer", TOGGLE, MAIN, &f_speed},
    {SETTINGS, "Show Position", TOGGLE, MAIN, &f_pos},
    {SETTINGS, "No Fog", TOGGLE, MAIN, &f_noFog},
    {SETTINGS, "Leave Yeeps", ACTION, MAIN, nullptr, QuitGame},
    {SETTINGS, "Turn Everything Off", ACTION, MAIN, nullptr, AllOff},
};
static constexpr int kItems = sizeof(g_items) / sizeof(g_items[0]);
static Item g_returnItem = {ENABLED, "Exit Enabled Mods", OPEN, MAIN};

// Tooltips, shown under the menu while your finger is on a button (like ii's).
static const struct { const char *label, *tip; } kTips[] = {
    {"Settings", "Opens the settings."},
    {"Movement Mods", "Opens the movement mods."},
    {"Enabled Mods", "Shows every mod you have on."},
    {"Platforms", "Hold a grip to stand on a platform."},
    {"Fly", "Hold A to fly where you look."},
    {"Fast Fly", "Makes Fly a lot faster."},
    {"Velocity Fly", "Hold A to shoot where your hand points."},
    {"Car", "Hold A to drive where you look, floating just above the ground."},
    {"Car Speed", "Slow, normal or fast."},
    {"Teleport Gun", "Hold right grip to aim, trigger to teleport."},
    {"Iron Man", "Hold X and A to fly with your hands. Gauntlets, flames and buzz."},
    {"Noclip", "Hold right trigger to go through walls."},
    {"Speed Boost", "Faster and higher jumps."},
    {"No Gravity", "Turns your gravity off."},
    {"Low Gravity", "Jump higher, fall slower."},
    {"Objects", "Draw in the air and place shapes. Only you see them."},
    {"Draw", "Hold right trigger to draw in the air."},
    {"Draw Color", "Red, Orange, Yellow, Green, Cyan, Blue, Purple, White, Rainbow."},
    {"Draw Size", "Thin, Medium, Thick or Huge lines and shapes."},
    {"Undo Drawing", "Removes your last drawing."},
    {"Place Cube", "Puts a cube in front of you, in the draw color and size."},
    {"Place Sphere", "Puts a sphere in front of you, in the draw color and size."},
    {"Clear Objects", "Removes everything you drew or placed."},
    {"Air Brake", "Hold X to stop dead in the air."},
    {"Jetpack", "Hold B to rise. Not with Right Hand Menu on."},
    {"Long Arms", "Stretches your reach. Only you see it stretched."},
    {"Speedometer", "Your speed in the title."},
    {"Show Position", "Your x, y, z in the title."},
    {"No Fog", "Turns the fog off for you."},
    {"Leave Yeeps", "Quits the game."},
    {"Save Spot", "Remembers where you are."},
    {"Go To Spot", "Teleports you to your saved spot."},
    {"Change Menu Theme", "Midnight, Orange, Blue, Green, Purple, Dark."},
    {"Rainbow Menu", "The menu cycles through colors."},
    {"Show FPS", "Frames per second in the title."},
    {"Show Clock", "The time in the title."},
    {"FPS/Clock On Screen", "Moves FPS/Clock to a small on-screen HUD."},
    {"Rig Mods", "Pick a dance, then hold left trigger. Everyone sees it."},
    {"Items", "Spawn copies of the item in your left hand. Everyone sees them."},
    {"Spawn Item Gun", "Hold an item in your left hand. Right grip aims, trigger spawns it."},
    {"Duplicate Items (3x3)", "9 of the item in your left hand appear in front of you."},
    {"Duplicate Items (3x3x3)", "27 of the item in your left hand appear in front of you."},
    {"Spawn On Hand", "Each right trigger spawns the item from your left hand at your right hand."},
    {"Wave", "Waves one hand, the other on your hip. Hold left trigger."},
    {"Helicopter", "Both arms spin around you. Hold left trigger."},
    {"T-Pose", "The classic T-pose. Hold left trigger."},
    {"Windmill", "Arms spin like a windmill. Hold left trigger."},
    {"Swim", "Swims through the air. Hold left trigger."},
    {"Jumping Jacks", "Arms go up and down at your sides. Hold left trigger."},
    {"Spaz", "Hands shake all over the place. Hold left trigger."},
    {"Beyblade", "Hands spin fast around your waist. Hold left trigger."},
    {"Skin Color", "Cycles your own skin color. Only you see it."},
    {"Eye Color", "Cycles your own eye color, if found separately from skin."},
    {"Skin Color", "Cycles your own skin color. Only you see it."},
    {"Eye Color", "Cycles your own eye color, if found separately from skin."},
    {"Change Pointer Position", "Moves the ball on your finger."},
    {"Iron Man Speed", "Changes how strong Iron Man is."},
    {"Right Hand Menu", "Hold B, menu on your right hand."},
    {"Notifications", "Messages when you switch something."},
    {"Turn Everything Off", "Switches every mod off."},
};
static const char *TipFor(const char *label) {
    if (strncmp(label, "Exit ", 5) == 0) return "Returns you back to the main category.";
    for (auto &t : kTips) if (strcmp(t.label, label) == 0) return t.tip;
    return "";
}

// Colors in the ii menu style: an animated background and darker buttons when on.
struct Theme { const char *name; Color bgA, bgB, button, buttonOn, title; };
static const Theme kThemes[] = {
    {"Midnight", Color(0.04f, 0.04f, 0.20f, 1), Color(0.10f, 0.10f, 0.44f, 1), Color(0.02f, 0.02f, 0.10f, 1), Color(0.30f, 0.40f, 1.0f, 1), Color(0.60f, 0.70f, 1.0f, 1)},
    {"Orange", Color(1.0f, 0.50f, 0.0f, 1), Color(1.0f, 0.40f, 0.0f, 1), Color(0.67f, 0.33f, 0.0f, 1), Color(0.33f, 0.16f, 0.0f, 1), Color(1.0f, 0.75f, 0.49f, 1)},
    {"Blue",   Color(0.20f, 0.50f, 1.0f, 1), Color(0.10f, 0.30f, 0.85f, 1), Color(0.10f, 0.28f, 0.65f, 1), Color(0.04f, 0.12f, 0.32f, 1), Color(0.70f, 0.85f, 1.0f, 1)},
    {"Green",  Color(0.30f, 0.85f, 0.30f, 1), Color(0.15f, 0.65f, 0.20f, 1), Color(0.12f, 0.45f, 0.12f, 1), Color(0.05f, 0.22f, 0.05f, 1), Color(0.75f, 1.0f, 0.75f, 1)},
    {"Purple", Color(0.65f, 0.30f, 1.0f, 1), Color(0.45f, 0.15f, 0.85f, 1), Color(0.38f, 0.15f, 0.60f, 1), Color(0.18f, 0.06f, 0.30f, 1), Color(0.88f, 0.75f, 1.0f, 1)},
    {"Dark",   Color(0.12f, 0.12f, 0.14f, 1), Color(0.05f, 0.05f, 0.06f, 1), Color(0.22f, 0.22f, 0.25f, 1), Color(0.45f, 0.45f, 0.50f, 1), Color(0.85f, 0.85f, 0.90f, 1)},
};
static constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
static int g_theme = 0;
static const Theme &T() { return kThemes[g_theme]; }

// ---- menu objects ----------------------------------------------------------------

// Layout in meters, matching ii's menu as it was ported to Yeeps. Everything is drawn
// on a "board" object: +x goes across, +y goes up, the player looks at it from -z.
static constexpr int kRows = 8;                                   // buttons per page
static const float kBoardW = 0.30f, kBoardH = 0.3825f, kBtnW = 0.27f, kBtnH = 0.0306f;
static const float kBoardR = 0.035f;                                 // rounded corners of the board
static RRect g_backPlate, g_backBadge, g_backBar[2];
static const float kTitleY = 0.235f, kSideX = 0.2f, kSideW = 0.06f, kSideH = 0.344f;
static float RowY(int r) { return 0.13f - r * 0.04f; }

static Ref g_menu, g_board, g_pointer, g_title;
static RRect g_bg, g_outline, g_titleBar, g_prev, g_next, g_rowCube[kRows];
static Ref g_rowLabel[kRows], g_rowText[kRows];
static Ref g_notifyGo, g_notifyText, g_tipText, g_leftPlatform, g_rightPlatform;
static double g_openTime = 0, g_flashUntil = 0;
static int g_flashRow = -2, g_hover = -2;
static bool g_built = false, g_menuShown = false, g_dirty = true;
static Cat g_cat = MAIN;
static int g_page = 0;
static std::vector<Item *> g_view;                                // what the current page list shows
static double g_time = 0, g_nextPress = 0, g_notifyUntil = 0, g_nextTitle = 0;
static int g_touching = -2, g_frames = 0;
static float g_fps = 0;

static int PageCount() { return g_view.empty() ? 1 : (int)(g_view.size() + kRows - 1) / kRows; }

static void BuildView() {
    g_view.clear();
    if (g_cat == ENABLED) {
        g_view.push_back(&g_returnItem);
        for (auto &it : g_items) if (it.kind == TOGGLE && *it.flag && it.flag != &f_notify) g_view.push_back(&it);
    } else {
        for (auto &it : g_items) if (it.cat == g_cat) g_view.push_back(&it);
    }
    if (g_page >= PageCount()) g_page = 0;
}

static Il2CppObject *Label(Il2CppObject *canvasT, const char *s, Vector3 pos, float w, float h, int size, Ref *textOut) {
    if (!G.font.get() || !G.tText) return nullptr;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("Label"));
    Il2CppObject *text = go ? G.goAddComponent[go](G.tText) : nullptr;
    if (!text) return nullptr;
    Il2CppObject *t = Tr(go);
    G.trSetParent[t](canvasT, false);
    G.rtSetSizeDelta[t](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[t](pos * 1000);
    G.trSetLocalRotation[t](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[t](Vector3(1, 1, 1));
    G.textSetFont[text](G.font.get());
    G.textSetText[text](CreateMonoString(s));
    G.textSetFontSize[text](size);
    G.textSetAlignment[text](4);                 // MiddleCenter
    if (G.textSetFontStyle.IsValid()) G.textSetFontStyle[text](2);   // Italic, like ii's menu
    if (G.textSetBestFit.IsValid()) {            // shrink long labels to fit their button
        G.textSetBestFit[text](true);
        G.textSetMinSize[text](1);
        G.textSetMaxSize[text](size);
    }
    G.graphicSetColor[text](Color(1, 1, 1, 1));
    if (textOut) textOut->set(text);
    return go;
}

// A world-space canvas (1 unit = 1 mm) under parent.
static Il2CppObject *Canvas(Il2CppObject *parentT, Vector3 pos, float w, float h) {
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("StarterMenuText"));
    Il2CppObject *canvas = go && G.tCanvas ? G.goAddComponent[go](G.tCanvas) : nullptr;
    if (!canvas) return nullptr;
    G.canvasSetRenderMode[canvas](2);            // WorldSpace
    Il2CppObject *ct = Tr(go);
    G.trSetParent[ct](parentT, false);
    G.rtSetSizeDelta[ct](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[ct](pos);
    G.trSetLocalRotation[ct](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[ct](Vector3(0.001f, 0.001f, 0.001f));
    return go;
}

static Il2CppObject *Part(Il2CppObject *parentT, Color c, Vector3 pos, Vector3 scale) {
    Il2CppObject *o = Cube(c, false);
    if (!o) return nullptr;
    Il2CppObject *t = Tr(o);
    G.trSetParent[t](parentT, false);
    G.trSetLocalPosition[t](pos);
    G.trSetLocalScale[t](scale);
    return o;
}

// Where the ball sits on your hand (ii's four pointer positions).
static const Vector3 kPointerSpots[] = {Vector3(0.01f, -0.017f, 0.19f), Vector3(0, -0.1f, 0.05f),
                                        Vector3(0, 0.1f, -0.05f), Vector3(0, 0.0666f, 0.1f)};
static int g_pointerSpot = 0;

static void PointerToHand() {
    Il2CppObject *p = g_pointer.get(), *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get();
    if (!Alive(p) || !hand) return;
    Il2CppObject *t = Tr(p);
    Vector3 at = kPointerSpots[g_pointerSpot];
    if (f_rightHand) at.x = -at.x;
    G.trSetParent[t](hand, false);
    G.trSetLocalPosition[t](at);
}
static void NextPointer() { g_pointerSpot = (g_pointerSpot + 1) % 4; PointerToHand(); Notify("Pointer position %d", g_pointerSpot + 1); }

// Built once per room; afterwards only shown/hidden, moved and relabeled.
static void BuildMenu() {
    auto NewObject = [](const char *name) {
        return Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString(name));
    };
    Il2CppObject *menu = NewObject("LinearClient"), *board = NewObject("LinearClientBoard");
    if (!menu || !board) return;
    g_menu.set(menu);
    g_board.set(board);
    Il2CppObject *bt = Tr(board);
    G.trSetParent[bt](Tr(menu), false);
    // Like ii's menu: the board stands 10 cm out from the hand on its +x side, facing +x.
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));

    // Everything is rounded: the board has curved corners, buttons are pills.
    RRBuild(g_outline, bt, T().title, Vector3(0, 0, 0.004f), kBoardW + 0.008f, kBoardH + 0.008f, 0.004f, kBoardR + 0.004f);
    RRBuild(g_bg, bt, T().bgA, Vector3(0, 0, 0), kBoardW, kBoardH, 0.01f, kBoardR);
    for (int r = 0; r < kRows; r++)
        RRBuild(g_rowCube[r], bt, T().button, Vector3(0, RowY(r), -0.005f), kBtnW, kBtnH, 0.009f, 1.0f);
    RRBuild(g_prev, bt, T().button, Vector3(-kSideX, 0, 0), kSideW, kSideH, 0.009f, kSideW / 2);
    // The title sits on its own bar above the board (where ii's Disconnect button was)
    RRBuild(g_titleBar, bt, T().buttonOn, Vector3(0, kTitleY, -0.002f), kBtnW, 0.045f, 0.009f, 1.0f);
    RRBuild(g_next, bt, T().button, Vector3(kSideX, 0, 0), kSideW, kSideH, 0.009f, kSideW / 2);

    if (Il2CppObject *canvas = Canvas(bt, Vector3(0, 0, -0.0105f), kBoardW + 2 * kSideX, kBoardH + 0.1f)) {
        Il2CppObject *ct = Tr(canvas);
        Label(ct, "Linear Client", Vector3(0, kTitleY, 0), kBtnW, 0.045f, 32, &g_title);
        for (int r = 0; r < kRows; r++)
            g_rowLabel[r].set(Label(ct, "", Vector3(0, RowY(r), 0), kBtnW, kBtnH, 19, &g_rowText[r]));
        Label(ct, "<", Vector3(-kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr);
        Label(ct, ">", Vector3(kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr);
        Label(ct, "", Vector3(0, -kBoardH / 2 - 0.02f, 0), kBoardW + 0.1f, 0.03f, 15, &g_tipText);
        char ver[64];
        snprintf(ver, sizeof ver, "Linear Client %s", ym::api()->gameVersion);
        Label(ct, ver, Vector3(0, -kBoardH / 2 + 0.012f, 0), kBoardW, 0.02f, 11, nullptr);
    }
    // The back of the board: a vector version of the Linear logo (rounded square,
    // a blue badge reading "Linear", and two slanted bars), since the mod can't load
    // an image file into a texture, only build shapes.
    RRBuild(g_backPlate, bt, Color(0.04f, 0.10f, 0.12f, 1), Vector3(0, 0, -0.016f), kBoardW, kBoardH, 0.006f, kBoardR);
    RRBuild(g_backBadge, bt, Color(0.12f, 0.12f, 0.95f, 1), Vector3(0.035f, 0, -0.019f), kBoardW * 0.62f, kBoardH * 0.26f, 0.004f, kBoardH * 0.26f / 2);
    auto RotZ = [](float deg) { float r = deg * 0.0174533f * 0.5f; return Quaternion(0, 0, sinf(r), cosf(r)); };
    for (int i = 0; i < 2; i++) {
        RRBuild(g_backBar[i], bt, i == 0 ? Color(0.20f, 0.18f, 0.55f, 1) : Color(0.30f, 0.26f, 0.85f, 1),
                Vector3(-kBoardW * 0.34f - i * 0.028f, -0.01f + i * 0.045f, -0.019f), 0.028f, kBoardH * 0.34f, 0.004f, 0.014f);
        G.trSetLocalRotation[Tr(g_backBar[i].root.get())](RotZ(18.0f));
    }
    if (Il2CppObject *bc = Canvas(bt, Vector3(0.035f, 0, -0.021f), kBoardW, kBoardH)) {
        Label(Tr(bc), "Linear", Vector3(0, 0, 0), kBoardW * 0.55f, kBoardH * 0.22f, 30, nullptr);
    }

    // Pointer: a small ball on the index finger of the other hand
    if (Il2CppObject *p = Prim(0, T().bgB, false)) {
        G.trSetLocalScale[Tr(p)](Vector3(0.01f, 0.01f, 0.01f));
        g_pointer.set(p);
        PointerToHand();
    }
    // Notifications: a line of text low in your view, like ii's notifications
    if (Il2CppObject *n = Canvas(g_head.get(), Vector3(0, -0.12f, 0.6f), 0.5f, 0.05f)) {
        g_notifyGo.set(n);
        Label(Tr(n), "", Vector3(0, 0, 0), 0.5f, 0.05f, 22, &g_notifyText);
        G.goSetActive[n](false);
    }
    G.goSetActive[menu](false);
    Show(g_pointer, false);
    g_built = true;
    g_dirty = true;
}

static void Notify(const char *fmt, ...) {
    if (!f_notify || !Alive(g_notifyGo.get())) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SetText(g_notifyText, buf);
    G.goSetActive[g_notifyGo.get()](true);
    g_notifyUntil = g_time + 2.0;
}

// Relabels and recolors the rows. Only runs after something changed.
static void RenderPage() {
    BuildView();
    for (int r = 0; r < kRows; r++) {
        int i = g_page * kRows + r;
        bool used = i < (int)g_view.size();
        RRShow(g_rowCube[r], used);
        Show(g_rowLabel[r], used);
        if (!used) continue;
        Item *it = g_view[i];
        bool on = it->kind == TOGGLE && *it->flag;
        SetText(g_rowText[r], it->label);
        RRColor(g_rowCube[r], r == g_flashRow ? Color(0.8f, 0.9f, 1.0f, 1) : on ? T().buttonOn : T().button);
        if (Alive(g_rowText[r].get())) G.graphicSetColor[g_rowText[r].get()](on ? Color(1, 1, 1, 1) : T().title);
    }
    bool pages = PageCount() > 1;
    RRShow(g_prev, pages);
    RRShow(g_next, pages);
    RRColor(g_prev, T().button);
    RRColor(g_next, T().button);
    RRColor(g_outline, T().title);
    RRColor(g_titleBar, T().buttonOn);
    SetColor(g_pointer.get(), T().bgB);
    if (Alive(g_title.get())) G.graphicSetColor[g_title.get()](Color(1, 1, 1, 1));
    g_nextTitle = 0;                     // refresh the title now
    g_dirty = false;
}

static int Hex(float v) { return (int)(v < 0 ? 0 : v > 1 ? 255 : v * 255); }

// The title is "Linear" with a moving midnight-blue gradient, one color per letter
// (rich text). Rebuilt ten times a second, not every frame.
static void UpdateTitle() {
    if (g_time < g_nextTitle) return;
    g_nextTitle = g_time + 0.1;
    static const Color kFire[] = {Color(0.25f, 0.35f, 1, 1), Color(0.55f, 0.70f, 1, 1), Color(0.85f, 0.92f, 1, 1), Color(0.55f, 0.70f, 1, 1)};
    char buf[512];
    int n = 0;
    const char *word = "Linear";
    for (int i = 0; word[i]; i++) {
        float t = (float)g_time * 1.5f - i * 0.35f;
        Color c;
        if (f_rainbow) c = Rainbow(t * 0.2f);
        else {
            float f = t - floorf(t / 4) * 4;          // 0..4 along the fire colors
            int k = (int)f;
            c = Lerp(kFire[k], kFire[(k + 1) % 4], f - k);
        }
        n += snprintf(buf + n, sizeof buf - n, "<b><color=#%02X%02X%02X>%c</color></b>", Hex(c.r), Hex(c.g), Hex(c.b), word[i]);
    }
    n += snprintf(buf + n, sizeof buf - n, " <color=grey>[</color><color=white>%d</color><color=grey>]</color>", g_page + 1);
    if (f_fps && !f_hud) n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d fps</size>", (int)(g_fps + 0.5f));
    if (f_clock && !f_hud) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d:%02d</size>", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    }
    if (f_speed) {
        Vector3 v;
        if (GetVel(v)) n += snprintf(buf + n, sizeof buf - n, "<size=16>  %.1f m/s</size>", sqrtf(v.x * v.x + v.y * v.y + v.z * v.z));
    }
    if (f_pos) {
        Vector3 p;
        if (BodyPos(p)) n += snprintf(buf + n, sizeof buf - n, "<size=14>  %.1f %.1f %.1f</size>", p.x, p.y, p.z);
    }
    SetText(g_title, buf);
}

static void ShowMenu(bool show) {
    if (show == g_menuShown) return;
    g_menuShown = show;
    if (show) { g_openTime = g_time; g_dirty = true; g_hover = -2; SetText(g_tipText, ""); }
    Show(g_menu, show);
    Show(g_pointer, show);
    g_touching = -2;
}

static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Like ii's menu: the menu follows your hand and is tipped 45 degrees, so you read it
// by turning your palm toward your face. "Floating Menu" instead keeps the board above
// your hand, always facing you.
static void PlaceMenu() {
    Il2CppObject *menu = g_menu.get(), *board = g_board.get(), *head = g_head.get();
    Il2CppObject *hand = f_rightHand ? g_rightHand.get() : g_leftHand.get();
    if (!Alive(menu) || !Alive(board) || !hand || !head) return;
    Il2CppObject *t = Tr(menu), *bt = Tr(board);
    Vector3 handPos = G.trGetPosition[hand]();
    if (!G.trGetRotation.IsValid()) {                  // fallback: float above the hand, facing you
        Vector3 center = handPos + Vector3(0, 0.25f, 0), away = center - G.trGetPosition[head]();
        if (away.x * away.x + away.y * away.y + away.z * away.z < 1e-6f) return;
        G.trSetPosition[t](handPos);
        G.trSetPosition[bt](center);
        G.trSetRotation[bt](Quaternion::LookRotation(away));
        return;
    }
    const float s = 0.38268343f, c = 0.92387953f;                  // sin/cos of 22.5 degrees
    Quaternion rot = G.trGetRotation[hand]() * Quaternion(-s, 0, 0, c);   // tip -45 degrees on x
    if (f_rightHand) rot = rot * Quaternion(0, 0, 1, 0);           // mirrored: 180 degrees on z
    G.trSetPosition[t](handPos);
    G.trSetRotation[t](rot);
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));
}

static void Press(Item *it) {
    switch (it->kind) {
    case OPEN:
        g_cat = it->target;
        g_page = 0;
        break;
    case TOGGLE:
        *it->flag = !*it->flag;
        if (it->fn) it->fn();
        Notify("[%s] %s", *it->flag ? "ON" : "OFF", it->label);
        break;
    case ACTION:
        if (it->fn) it->fn();
        break;
    }
    g_dirty = true;
    ym::log("linear-client: %s", it->label);
}

// Which part of the menu the pointer touches: row 0..7, -1 = <, kRows = >, -2 = nothing.
static int Touching() {
    Il2CppObject *board = g_board.get(), *pointer = g_pointer.get();
    if (!Alive(board) || !Alive(pointer)) return -2;
    Vector3 p = G.trInverseTransformPoint[Tr(board)](G.trGetPosition[Tr(pointer)]());
    if (p.z < -0.035f || p.z > 0.015f) return -2;
    if (fabsf(fabsf(p.x) - kSideX) < kSideW / 2 && fabsf(p.y) < kSideH / 2 && PageCount() > 1)
        return p.x < 0 ? -1 : kRows;
    if (fabsf(p.x) >= kBtnW / 2) return -2;
    for (int r = 0; r < kRows; r++)
        if (fabsf(p.y - RowY(r)) < kBtnH / 2) return r;
    return -2;
}

static void Buzz() {
    if (!G.haptic.IsValid()) return;
    XRDevice d = G.deviceAtNode(f_rightHand ? 4 : 5);    // the hand that presses
    if (d.initialized) G.haptic(d.id, 0u, 0.35f, 0.04f);
}

static void CheckPress() {
    int hit = Touching();
    if (hit != g_hover) {                                 // tooltip for the button under your finger
        g_hover = hit;
        int i = g_page * kRows + hit;
        if (hit >= 0 && hit < kRows && i < (int)g_view.size()) SetText(g_tipText, TipFor(g_view[i]->label));
    }
    if (hit != -2 && hit != g_touching && g_time >= g_nextPress) {   // one press per touch
        g_nextPress = g_time + 0.25;
        Buzz();
        if (hit >= 0 && hit < kRows) { g_flashRow = hit; g_flashUntil = g_time + 0.12; }
        if (hit == -1 || hit == kRows) {
            int n = PageCount();
            g_page = (g_page + (hit == kRows ? 1 : n - 1)) % n;
            g_dirty = true;
        } else {
            int i = g_page * kRows + hit;
            if (i < (int)g_view.size()) Press(g_view[i]);
        }
    }
    g_touching = hit;
}

static void AnimateMenu() {
    float open = (float)(g_time - g_openTime) / 0.15f;     // pop open like ii's menu
    if (open <= 1.05f && Alive(g_menu.get())) {
        float k = open >= 1 ? 1 : 1 - (1 - open) * (1 - open);
        G.trSetLocalScale[Tr(g_menu.get())](Vector3(k, k, k));
    }
    if (g_flashRow != -2 && g_time >= g_flashUntil) { g_flashRow = -2; g_dirty = true; }
    static double nextBg = 0;                              // the background shimmer runs at 20 Hz
    if (g_time >= nextBg) {
        nextBg = g_time + 0.05;
        float t = 0.5f + 0.5f * sinf((float)g_time * 2.0f);
        Color c = f_rainbow ? Rainbow((float)g_time * 0.15f) : Lerp(T().bgA, T().bgB, t);
        RRColor(g_bg, c);
    }
}

// When the menu opens, the hand that wears it buzzes, fading in over 0.4 s.
static void OpenBuzzTick() {
    static double next = 0;
    double age = g_time - g_openTime;
    if (age > 0.4 || age < 0 || !G.haptic.IsValid() || g_time < next) return;
    next = g_time + 0.04;
    XRDevice d = G.deviceAtNode(f_rightHand ? 5 : 4);
    if (d.initialized) G.haptic(d.id, 0u, 0.03f + 0.32f * (float)(age / 0.4), 0.06f);
}

// ---- features (local only) -----------------------------------------------------

static Il2CppObject *PlayerTransform() {
    Il2CppObject *player = g_player.get();
    return player ? G.compTransform[player]() : nullptr;
}

// Your body's velocity, through the game's own player functions (Rigidbody as backup).
static bool GetVel(Vector3 &v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plGetVelocity.IsValid()) { v = G.plGetVelocity[player](); return true; }
    if (rb && G.rbGetVelocity.IsValid()) { v = G.rbGetVelocity[rb](); return true; }
    return false;
}
static void SetVel(Vector3 v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plSetVelocity.IsValid()) G.plSetVelocity[player](v);
    else if (rb && G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](v);
}

// Where your body is (the body collider), which is what teleports aim for.
static bool BodyPos(Vector3 &out) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    if (!player || !t) return false;
    Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
    out = body ? G.trGetPosition[G.compTransform[body]()]() : G.trGetPosition[t]();
    return true;
}
static void StopMoving() { SetVel(Vector3(0, 0, 0)); }
// Moves your body to pos, using the game's own TeleportTo when it has one.
static void TeleportPlayer(Vector3 pos) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    Vector3 body;
    if (!player || !t || !BodyPos(body)) return;
    Vector3 target = pos - body + G.trGetPosition[t]();
    if (G.teleportTo.IsValid()) G.teleportTo[player](target);
    else G.trSetPosition[t](target);
    StopMoving();
}

static void SaveSpot() {
    if (BodyPos(g_spot)) { g_haveSpot = true; Notify("Spot saved"); }
}
static void GoToSpot() {
    if (!g_haveSpot) { Notify("Save a spot first"); return; }
    TeleportPlayer(g_spot);
    Notify("Teleported to your spot");
}

static const float kIronSpeeds[] = {15, 12, 8, 4};
static int g_ironSpeed = 0;
static void NextIronSpeed() { g_ironSpeed = (g_ironSpeed + 1) % 4; Notify("Iron Man speed: %d", (int)kIronSpeeds[g_ironSpeed]); }
static void NextTheme() { g_theme = (g_theme + 1) % kThemeCount; Notify("Theme: %s", T().name); }
static void AllOff() {
    for (auto &it : g_items) if (it.kind == TOGGLE && it.flag != &f_notify) *it.flag = false;   // ticks undo each mod
    g_dance = -1;
    g_dupeArmed = 0;
    g_itemQueue.clear();
    PointerToHand();
    Notify("Everything off");
}

static void FlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *t = PlayerTransform();
    if (!f_fly || !in.a || !head || !t) return;
    Vector3 pos = G.trGetPosition[t]() + G.trGetForward[head]() * ((f_fastFly ? 20.0f : 8.0f) * dt);
    G.trSetPosition[t](pos);
    StopMoving();
}

static void LowGravityTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_lowGravity || f_noGravity || (f_fly && in.a) || !GetVel(v)) return;
    SetVel(v + Vector3(0, 9.81f * 0.7f * dt, 0));       // cancels 70% of gravity
}

static void PlatformFor(bool held, Ref &platform, Ref &hand) {
    if (!held || !f_platforms) {
        Kill(platform);
        return;
    }
    if (platform.get()) return;                             // already standing on one
    if (!hand.get()) return;
    Il2CppObject *p = Cube(T().button, true);               // keeps its collider: you stand on it
    if (!p) return;
    // Hands only collide with the layers in the player's locomotionEnabledLayers (like
    // Gorilla Tag), so the platform goes on the first of those layers.
    static int layer = -2;
    if (layer == -2) {
        layer = -1;
        int mask = G.locomotionLayers.IsValid() && g_player.get() ? G.locomotionLayers[g_player.get()]() : 0;
        for (int i = 0; i < 32 && layer < 0; i++) if (mask & (1 << i)) layer = i;
        ym::log("linear-client: hand layers 0x%x, platforms on layer %d", mask, layer);
    }
    if (layer >= 0 && G.goSetLayer.IsValid()) G.goSetLayer[p](layer);
    Il2CppObject *t = Tr(p);
    G.trSetPosition[t](G.trGetPosition[hand.get()]() - Vector3(0, 0.08f, 0));
    G.trSetLocalScale[t](Vector3(0.28f, 0.02f, 0.28f));
    platform.set(p);
}

static void VelocityFlyTick(const Buttons &in) {
    Il2CppObject *hand = g_rightHand.get();
    if (!f_velocityFly || !in.a || !hand) return;
    SetVel(G.trGetForward[hand]() * 20.0f);
}

static Vector3 Flat(Vector3 v);

// Car: like Fly, but along the ground. Holding A drives you the way you look, flat, and
// you hover a little above the ground (a raycast down finds it) so the bumps on blocks
// don't catch you. Off a ledge, with no ground close below, you fall normally.
static const float kCarSpeeds[] = {4.0f, 6.0f, 9.0f};
static const char *kCarNames[] = {"slow", "normal", "fast"};
static int g_carSpeed = 1;
static float g_carHeight = -1;               // your head's height above the ground, measured once
static void NextCarSpeed() { g_carSpeed = (g_carSpeed + 1) % 3; Notify("Car: %s", kCarNames[g_carSpeed]); }
static void CarTick(const Buttons &in) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    Vector3 v;
    if (!f_car) { g_carHeight = -1; return; }
    if (!in.a || !head || !player || !GetVel(v) || !G.raycast.IsValid()) return;
    int mask = G.locomotionLayers.IsValid() ? G.locomotionLayers[player]() : -5;
    Vector3 hp = G.trGetPosition[head]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool ground = G.raycast(hp, Vector3(0, -1, 0), &hit, 3.0f, mask ? mask : -5);
    Vector3 f = Flat(G.trGetForward[head]()) * kCarSpeeds[g_carSpeed];
    float vy = fminf(v.y, 1.0f);
    if (ground) {
        if (g_carHeight < 0) g_carHeight = hp.y - hit.point.y;      // your standing height
        float target = hit.point.y + g_carHeight + 0.15f;            // float 15 cm higher
        vy = (target - hp.y) * 8.0f;
        if (G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.1f);
    }
    SetVel(Vector3(f.x, vy, f.z));
}

static void IronManTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_ironMan || !G.trGetRight.IsValid() || (!in.x && !in.a) || !GetVel(v)) return;
    float speed = kIronSpeeds[g_ironSpeed] * dt;
    if (in.x && g_leftHand.get()) v = v + G.trGetRight[g_leftHand.get()]() * -speed;
    if (in.a && g_rightHand.get()) v = v + G.trGetRight[g_rightHand.get()]() * speed;
    SetVel(v);
}

// Speed Boost and No Gravity change your own player; turning them off puts the old
// values back. Checked every frame so they also come back after a room change.
static float g_oldJumpSpeed = 0, g_oldJumpMultiplier = 0;
static Il2CppObject *g_boosted = nullptr;      // which player has the boost (compared only)
static bool g_gravityOff = false;

static void ApplySpeedBoost() {
    Il2CppObject *player = g_player.get();
    bool ok = G.maxJumpSpeed.IsValid() && G.jumpMultiplier.IsValid();
    if (f_speedBoost && ok && player && g_boosted != player) {
        g_oldJumpSpeed = G.maxJumpSpeed[player]();
        g_oldJumpMultiplier = G.jumpMultiplier[player]();
        G.maxJumpSpeed[player] = 9.0f;
        G.jumpMultiplier[player] = 1.5f;
        g_boosted = player;
    } else if (!f_speedBoost && g_boosted) {
        if (ok && player == g_boosted) {
            G.maxJumpSpeed[player] = g_oldJumpSpeed;
            G.jumpMultiplier[player] = g_oldJumpMultiplier;
        }
        g_boosted = nullptr;
    }
}

static void ApplyGravity() {
    // The game's own "no gravity for a while" (what its levitation items use), renewed
    // every frame while the mod is on; it wears off by itself once you turn it off.
    Il2CppObject *player = g_player.get();
    if (f_noGravity && player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
    g_gravityOff = f_noGravity;
}

// Noclip: while you hold the right trigger you pass through everything. Yeeps moves
// your body with its own layer lists (locomotionEnabledLayers for your hands,
// collisionEnabledLayers for your body), so those are emptied, physics ignores your
// body against those layers, and your own colliders are off. Gravity stays normal, like
// Gorilla Tag noclip. All of it goes back when you let go. Only your own player changes; nothing is sent.
static bool g_noclipOn = false;
static Ref g_noclipOff[24];                    // your own colliders switched off during noclip
static int g_noclipOffN = 0;
// Every solid collider on your own player (body, head, hands...), switched off while
// noclip is held so nothing of you hits the level.
static void PlayerColliders(bool off) {
    if (!off) {
        for (int i = 0; i < g_noclipOffN; i++) {
            if (Alive(g_noclipOff[i].get())) G.colliderSetEnabled[g_noclipOff[i].get()](true);
            g_noclipOff[i].clear();
        }
        g_noclipOffN = 0;
        return;
    }
    Il2CppObject *player = g_player.get();
    if (!player || !G.colliderSetEnabled.IsValid()) return;
    auto offOne = [](Il2CppObject *c) {
        if (!Alive(c) || g_noclipOffN >= 24 || !G.behaviourGetEnabledCollider[c]()) return;
        if (G.colliderIsTrigger.IsValid() && G.colliderIsTrigger[c]()) return;
        for (int i = 0; i < g_noclipOffN; i++) if (g_noclipOff[i].get() == c) return;
        G.colliderSetEnabled[c](false);
        g_noclipOff[g_noclipOffN++].set(c);
    };
    // Your body and head colliders (fields on the player), then every solid collider in
    // the rig they belong to.
    Il2CppObject *own[2] = {G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr,
                            G.headCollider.IsValid() ? G.headCollider[player]() : nullptr};
    for (Il2CppObject *c : own) {
        if (!c) continue;
        offOne(c);
        Il2CppObject *rig = G.compGameObject[G.trGetRoot[G.compTransform[c]()]()]();
        auto *arr = rig && G.getComponentsInChildren.IsValid()
                        ? (uint8_t *)G.getComponentsInChildren[rig]((Il2CppObject *)G.tCollider, false) : nullptr;
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (size_t i = 0; i < n; i++) offOne(((Il2CppObject **)(arr + 0x20))[i]);
    }
}
static int g_savedLoco = 0, g_savedColl = 0, g_bodyLayer = -1;
static void NoclipTick(const Buttons &in) {
    Il2CppObject *player = g_player.get();
    bool want = f_noclip && in.rtrig && player;
    if (want == g_noclipOn) return;
    g_noclipOn = want;
    if (!player || !G.locomotionLayers.IsValid()) return;
    if (want) {
        g_savedLoco = G.locomotionLayers[player]();
        g_savedColl = G.collisionLayers.IsValid() ? G.collisionLayers[player]() : 0;
        Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
        Il2CppObject *bodyGo = body ? G.compGameObject[body]() : nullptr;
        g_bodyLayer = bodyGo && G.goGetLayer.IsValid() ? G.goGetLayer[bodyGo]() : -1;
    }
    int mask = g_savedLoco | g_savedColl;
    G.locomotionLayers[player] = want ? 0 : g_savedLoco;
    if (G.collisionLayers.IsValid()) G.collisionLayers[player] = want ? 0 : g_savedColl;
    if (g_bodyLayer >= 0 && G.ignoreLayers.IsValid())
        for (int l = 0; l < 32; l++)
            if (mask & (1 << l)) G.ignoreLayers(g_bodyLayer, l, want);
    PlayerColliders(want);
    if (want) ym::log("linear-client: noclip on (layers 0x%x, body layer %d, %d colliders off)", mask, g_bodyLayer, g_noclipOffN);
}

// Teleport Gun: hold right grip to aim (a line and a ball show where you land), press
// the right trigger to go there.
static Ref g_gunLine, g_gunLineGo, g_gunBall;
static bool g_gunTrigger = false;
static void TeleportGunTick(const Buttons &in) {
    bool aiming = (f_tpGun || f_itemGun) && in.rgrip;
    if (!aiming) {
        Show(g_gunLineGo, false);
        Show(g_gunBall, false);
        g_gunTrigger = in.rtrig;
        return;
    }
    Il2CppObject *hand = g_rightHand.get();
    if (!hand || !G.raycast.IsValid() || !G.lrSetPosition.IsValid()) return;
    if (!g_gunBall.get()) {                              // made once per room
        Il2CppObject *ball = Prim(0, T().bgB, false);
        Il2CppObject *lineGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LinearGunLine"));
        Il2CppObject *line = lineGo && G.tLine ? G.goAddComponent[lineGo](G.tLine) : nullptr;
        if (!ball || !line) return;
        G.trSetLocalScale[Tr(ball)](Vector3(0.1f, 0.1f, 0.1f));
        if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
            if (G.shader.get()) G.matSetShader[m](G.shader.get());
            G.matSetColor[m](T().bgB);
        }
        G.lrSetStartWidth[line](0.025f);
        G.lrSetEndWidth[line](0.025f);
        G.lrSetPositionCount[line](2);
        G.lrSetUseWorldSpace[line](true);
        g_gunBall.set(ball);
        g_gunLine.set(line);
        g_gunLineGo.set(lineGo);
    }
    Vector3 start = G.trGetPosition[hand](), dir = G.trGetForward[hand]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool found = G.raycast(start + dir * 0.25f, dir, &hit, 512.0f, -5);   // Physics.DefaultRaycastLayers
    Vector3 end = found ? hit.point : start + dir * 512.0f;
    Show(g_gunLineGo, true);
    Show(g_gunBall, true);
    G.trSetPosition[Tr(g_gunBall.get())](end);
    G.lrSetPosition[g_gunLine.get()](0, start);
    G.lrSetPosition[g_gunLine.get()](1, end);
    SetColor(g_gunBall.get(), in.rtrig ? T().buttonOn : T().bgB);
    if (in.rtrig && !g_gunTrigger && found) {
        if (f_itemGun) {                                  // Spawn Item Gun: an item at that spot
            if (g_time >= g_nextItem && LeftHandItem()) {
                g_nextItem = g_time + 0.5;
                g_itemQueue.push_back({end + Vector3(0, 0.3f, 0), Vector3(0, 0, 0)});
            }
        } else TeleportPlayer(end + Vector3(0, 1.0f, 0));
    }
    g_gunTrigger = in.rtrig;
}

// ---- vector helpers ----------------------------------------------------------------

static Vector3 Cross(Vector3 a, Vector3 b) { return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static Vector3 Flat(Vector3 v) {                      // v without its up/down part, length 1
    v.y = 0;
    float l = sqrtf(v.x * v.x + v.z * v.z);
    return l > 1e-4f ? v * (1.0f / l) : Vector3(0, 0, 1);
}

// ---- rig mods: dances -----------------------------------------------------------
// While you hold the left trigger, your hands are moved through a dance instead of
// following your controllers. The game sends your hand positions to the room as
// usual, so other players see you dance. Nothing extra is sent.

// The dance buttons work like radio buttons: picking one turns the others off,
// pressing the picked one again turns dancing off.
static void DancePicked() {
    int picked = -1;
    for (int i = 0; i < kDances; i++) if (g_danceSel[i] && i != g_dance) picked = i;
    for (int i = 0; i < kDances; i++) g_danceSel[i] = (i == picked);
    g_dance = picked;
    if (picked >= 0) Notify("Hold left trigger to dance");
}
static float Rand01() { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (x & 0xffff) / 65535.0f; }

// The controller trackers would put your hands back every frame, so they are paused
// while you dance (on the hand and the two objects above it) and turned back on after.
static Ref g_paused[12];
static int g_pausedN = 0;
static bool g_dancing = false;
static void PauseTrackers(bool pause) {
    if (!pause) {
        for (int i = 0; i < g_pausedN; i++) {
            if (Alive(g_paused[i].get())) G.behaviourSetEnabled[g_paused[i].get()](true);
            g_paused[i].clear();
        }
        g_pausedN = 0;
        return;
    }
    if (!G.behaviourSetEnabled.IsValid() || !G.trGetParent.IsValid()) return;
    for (Il2CppObject *t : {g_leftHand.get(), g_rightHand.get()})
        for (int up = 0; up < 3 && t; up++, t = G.trGetParent[t]())
            for (BNM::MonoType *type : G.trackerTypes) {
                Il2CppObject *go = G.compGameObject[t]();
                Il2CppObject *c = go ? G.goGetComponent[go]((Il2CppObject *)type) : nullptr;
                if (!c || !G.behaviourGetEnabled[c]() || g_pausedN >= 12) continue;
                G.behaviourSetEnabled[c](false);
                g_paused[g_pausedN++].set(c);
            }
}

static void DanceTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    bool want = g_dance >= 0 && in.ltrig && head && lh && rh;
    if (want != g_dancing) {
        PauseTrackers(want);
        g_dancing = want;
        if (want) ym::log("linear-client: dancing, paused %d trackers", g_pausedN);
    }
    if (!want) return;
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), up(0, 1, 0);
    Vector3 r = Cross(up, f);                            // your right
    Vector3 shoulder = hp + up * -0.25f;
    float t = (float)g_time, s = sinf(t * 6), c = cosf(t * 6);
    Vector3 L, R;                                        // hand positions
    switch (g_dance) {
    case 0:   // Wave: right hand up and waving, left hand on the hip
        R = shoulder + r * (0.35f + 0.12f * s) + up * 0.45f + f * 0.1f;
        L = shoulder + r * -0.25f + up * -0.25f + f * 0.05f;
        break;
    case 1:   // Helicopter: both hands spin around you
        R = shoulder + r * (0.6f * cosf(t * 8)) + f * (0.6f * sinf(t * 8)) + up * 0.15f;
        L = shoulder - r * (0.6f * cosf(t * 8)) - f * (0.6f * sinf(t * 8)) + up * 0.15f;
        break;
    case 2:   // T-Pose
        R = shoulder + r * 0.7f + up * 0.1f;
        L = shoulder - r * 0.7f + up * 0.1f;
        break;
    case 3:   // Windmill: big circles at your sides
        R = shoulder + r * 0.4f + up * (0.5f * sinf(-t * 5)) + f * (0.5f * cosf(-t * 5));
        L = shoulder - r * 0.4f - up * (0.5f * sinf(-t * 5)) - f * (0.5f * cosf(-t * 5));
        break;
    case 4: {  // Swim: breaststroke
        float p = t * 3;
        R = shoulder + f * (0.3f + 0.25f * cosf(p)) + r * (0.08f + 0.35f * fabsf(sinf(p)));
        L = shoulder + f * (0.3f + 0.25f * cosf(p)) - r * (0.08f + 0.35f * fabsf(sinf(p)));
        break;
    }
    case 5: {  // Jumping jacks: arms from down by your sides to up over your head
        float a = (sinf(t * 6) * 0.5f + 0.5f) * 3.1f - 1.55f;
        R = shoulder + r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        L = shoulder - r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        break;
    }
    case 6:   // Spaz: hands in front, shaking everywhere
        R = shoulder + f * 0.35f + r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        L = shoulder + f * 0.35f - r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        break;
    default:  // Beyblade: hands spin fast around your waist
        R = shoulder - up * 0.3f + r * (0.5f * cosf(t * 20)) + f * (0.5f * sinf(t * 20));
        L = shoulder - up * 0.3f - r * (0.5f * cosf(t * 20)) - f * (0.5f * sinf(t * 20));
        break;
    }
    // Keep each hand within arm's reach of its shoulder, then glide there instead of
    // jumping, so the arms move smoothly for you and for everyone watching.
    auto reach = [&](Vector3 hand, Vector3 sh) {
        Vector3 d = hand - sh;
        float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        return len > 0.7f ? sh + d * (0.7f / len) : hand;
    };
    R = reach(R, shoulder + r * 0.18f);
    L = reach(L, shoulder - r * 0.18f);
    float k = g_dance == 6 ? 1.0f : fminf(1.0f, dt * 14.0f);     // Spaz stays twitchy
    G.trSetPosition[lh](G.trGetPosition[lh]() + (L - G.trGetPosition[lh]()) * k);
    G.trSetPosition[rh](G.trGetPosition[rh]() + (R - G.trGetPosition[rh]()) * k);
    Quaternion look = Quaternion::LookRotation(f, up);
    G.trSetRotation[lh](look);
    G.trSetRotation[rh](look);
}

// ---- items (everyone sees these) ------------------------------------------------
// Items are made with the game's own item message (RpcCreateItem on the room's
// NetworkMessenger), the same one the game sends when an item appears. Owner-approved
// 2026-10-31. Every way of spawning has a cooldown, and big batches are sent a few per
// frame, so the room can't be flooded.


static void Put(Il2CppObject *arr, size_t i, void *v) { ((void **)((uint8_t *)arr + 0x20))[i] = v; }

// Builds RpcCreateItem's arguments from its own parameter list, in the order the game
// uses: item id, item key, spawn kind 1, no owner, position, rotation, velocity, spin,
// properties, 1, 1.
static bool SendItem(const char *key, Vector3 pos, Quaternion rot, Vector3 vel, char *idOut = nullptr) {
    auto *mi = G.rpcCreateItem;
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!mi || !nm || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.tPhotonView) return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    // The last parameter (PhotonMessageInfo) is filled in by Photon on the receiving
    // side; it is never sent.
    int count = mi->parameters_count;
    if (count > 0 && TypeName(mi->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;
    char id[8];
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 4; i++) id[i] = kChars[(int)(Rand01() * 35.99f)];
    id[4] = 0;
    if (idOut) memcpy(idOut, id, 5);
    int strings = 0, vectors = 0, others = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(mi->parameters[i]);
        Class pc(mi->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;
        if (tn == "System.String") {
            if (strings == 0) v = CreateMonoString(id);
            else if (strings == 1) v = CreateMonoString(key);
            strings++;                                   // later strings stay null
        } else if (tn == "UnityEngine.Vector3") {
            Vector3 vec = vectors == 0 ? pos : vectors == 1 ? vel : Vector3(0, 0, 0);
            vectors++;
            v = G.valueBox(k, &vec);
        } else if (tn == "UnityEngine.Quaternion") {
            v = G.valueBox(k, &rot);
        } else if (tn == "System.String[]") {
            v = G.arrayNew(G.stringClass, 0);
        } else if (k && k->byval_arg.valuetype) {
            uint8_t raw[64] = {};
            raw[0] = 1;                                 // spawn kind 1 / the two bytes = 1
            // Photon can't send the game's enum types, only plain numbers (like the kit's
            // Int32 1), so an enum goes as its underlying number type.
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
            others++;
        }
        Put(args, i, v);
    }
    static bool loggedTypes = false;
    if (!loggedTypes) {                                   // once: what the game expects, for fixing
        loggedTypes = true;
        std::string sig;
        for (int i = 0; i < mi->parameters_count; i++) {
            Class pc(mi->parameters[i]);
            auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
            std::string tn = TypeName(mi->parameters[i]);
            if (tn.size() > 30) tn = k && k->byval_arg.valuetype ? (k->enumtype ? "enum" : "struct" + std::to_string(k->instance_size - 16)) : "class";
            sig += (i ? "," : "") + tn;
        }
        ym::log("linear-client: item message takes %s", sig.c_str());
    }
    try {
        G.pvRpc[pv](CreateMonoString("RpcCreateItem"), 0, args);      // 0 = everyone in the room
    } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {   // the game said no: log why, don't crash the menu
        auto *ex = w.ex;
        auto *msg = ex ? (BNM::Structures::Mono::String *)ex->message : nullptr;
        auto *cls = ex ? (BNM::Structures::Mono::String *)ex->className : nullptr;
        ym::log("linear-client: item message error %s: %s", cls ? cls->str().c_str() : "?", msg ? msg->str().c_str() : "?");
        return false;
    }
    return true;
}

static void QueueItems(const std::vector<PendingItem> &list) {
    if (!g_itemKey[0]) return;
    if (g_time < g_nextBatch) { Notify("Wait a moment"); return; }
    g_nextBatch = g_time + 3.0;
    for (auto &p : list) if (g_itemQueue.size() < 32) g_itemQueue.push_back(p);
}

static void ItemQueueTick() {
    for (int sent = 0; sent < 2 && !g_itemQueue.empty(); sent++) {   // 2 a frame at most
        PendingItem p = g_itemQueue.front();
        g_itemQueue.erase(g_itemQueue.begin());
        bool ok = SendItem(g_itemKey, p.pos, Quaternion(0, 0, 0, 1), p.vel);
        static int logged = 0;
        if (logged++ < 3) ym::log("linear-client: spawn %s ok=%d", g_itemKey, ok ? 1 : 0);
        if (!ok) { g_itemQueue.clear(); Notify("Item spawning isn't ready in this room"); }
    }
}

// The grid in front of you: 3 across, 3 deep, and 3 high for 3x3x3.
static void SpawnGrid(int count) {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    Vector3 f = Flat(G.trGetForward[head]()), r = Cross(Vector3(0, 1, 0), f);
    Vector3 base = G.trGetPosition[head]() + f * 1.2f + Vector3(0, -0.4f, 0);
    std::vector<PendingItem> list;
    for (int y = 0; y < (count > 9 ? 3 : 1); y++)
        for (int z = 0; z < 3; z++)
            for (int x = 0; x < 3; x++)
                list.push_back({base + r * ((x - 1) * 0.45f) + f * (z * 0.45f) + Vector3(0, y * 0.45f, 0), Vector3(0, 0, 0)});
    QueueItems(list);
}
// The item you're holding in your LEFT hand right now (owner 2026-10-31: items spawn as
// whatever is in your left hand when you press). One scene search per press.
static bool LeftHandItem() {
    Il2CppObject *hand = g_leftHand.get();
    if (!hand || !G.tItem || !G.itemKey.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return false;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Vector3 hp = G.trGetPosition[hand]();
    Il2CppObject *best = nullptr;
    float bestD = 0.4f * 0.4f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it) || (G.itemHeld.IsValid() && !G.itemHeld[it]())) continue;
        Vector3 d = G.trGetPosition[G.compTransform[it]()]() - hp;
        float dd = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dd < bestD) { bestD = dd; best = it; }
    }
    auto *key = best ? G.itemKey[best]() : nullptr;
    if (!key) { Notify("Hold an item in your left hand"); return false; }
    snprintf(g_itemKey, sizeof g_itemKey, "%s", key->str().c_str());
    return true;
}
static void ArmDupe9() { if (LeftHandItem()) SpawnGrid(9); }
static void ArmDupe27() { if (LeftHandItem()) SpawnGrid(27); }

// Spawn On Hand: each right-trigger press (grip open) spawns the item at your hand.
static bool g_onHandTrigger = false;
static void SpawnOnHandTick(const Buttons &in) {
    bool press = in.rtrig && !g_onHandTrigger;
    g_onHandTrigger = in.rtrig;
    Il2CppObject *hand = g_rightHand.get();
    if (!f_spawnOnHand || !press || in.rgrip || g_menuShown || !hand || g_time < g_nextItem) return;
    if (!LeftHandItem()) return;
    g_nextItem = g_time + 0.3;
    g_itemQueue.push_back({G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.1f, Vector3(0, 0, 0)});
}

// ---- Iron Man gauntlets, flames and haptics (local only) ----------------------------
// A glove on each hand while Iron Man is on. It follows the hand object the game gives us
// (the whole hand, not each finger). Flames show on the palm while that hand thrusts, and
// the controller buzzes.
struct Gauntlet { Ref plate, core, flame[3]; bool built = false, lit = false; };
static Gauntlet g_gaunt[2];                      // 0 = left, 1 = right
// Which side of the hand (local x) the palm is on. Iron Man pushes you the other way.
// If the glove or flames show up on the wrong side, flip the signs here.
static const float kPalmSide[2] = {1.0f, -1.0f};
// The left and right hand bones mirror each other, so a plate placed the same way on
// both ends up flipped on one side. A spin around local Z (forward) only fixed the
// left/right mirroring, not the up/down flip reported in testing, so this now spins
// the left hand 180 degrees around local X instead, which swaps up/down and
// front/back while leaving left/right alone. If it's STILL wrong, the quaternions to
// try by hand are: X spin Quaternion(1,0,0,0), Y spin Quaternion(0,1,0,0), Z spin
// Quaternion(0,0,1,0) (identity is Quaternion(0,0,0,1)); swap whichever looks right
// into index 0 below.
static const Quaternion kGauntletRot[2] = {Quaternion(1, 0, 0, 0), Quaternion(0, 0, 0, 1)};
static double g_nextHaptic = 0;

static void GauntletKill() {
    for (auto &g : g_gaunt) {
        Kill(g.plate);
        Kill(g.core);
        for (auto &f : g.flame) Kill(f);
        g.built = false;
        g.lit = false;
    }
}

static void GauntletBuild(int h, Il2CppObject *hand) {
    Gauntlet &g = g_gaunt[h];
    float s = kPalmSide[h];
    Il2CppObject *plate = Prim(3, Color(0.75f, 0.05f, 0.05f, 1), false);       // red armor on the back of the hand
    Il2CppObject *core = Prim(0, Color(0.65f, 0.92f, 1.0f, 1), false);         // glowing repulsor in the palm
    if (!plate || !core) {
        if (plate) G.destroy(plate);
        if (core) G.destroy(core);
        return;
    }
    auto place = [&](Il2CppObject *o, Vector3 pos, Vector3 scale) {
        Il2CppObject *t = Tr(o);
        G.trSetParent[t](hand, false);
        G.trSetLocalPosition[t](pos);
        G.trSetLocalRotation[t](kGauntletRot[h]);
        G.trSetLocalScale[t](scale);
    };
    place(plate, Vector3(-s * 0.045f, 0.0f, 0.04f), Vector3(0.02f, 0.075f, 0.11f));
    place(core, Vector3(s * 0.03f, 0.0f, 0.03f), Vector3(0.04f, 0.04f, 0.04f));
    g.plate.set(plate);
    g.core.set(core);
    static const Color kFlame[3] = {Color(1.0f, 0.35f, 0.05f, 1), Color(1.0f, 0.80f, 0.20f, 1), Color(0.75f, 0.92f, 1.0f, 1)};
    for (int i = 0; i < 3; i++) {
        Il2CppObject *f = Prim(0, kFlame[i], false);
        if (!f) continue;
        G.trSetParent[Tr(f)](hand, false);
        G.goSetActive[f](false);
        g.flame[i].set(f);
    }
    g.built = true;
}

static void IronGauntletTick(const Buttons &in) {
    bool on = f_ironMan && g_leftHand.get() && g_rightHand.get();
    if (!on) {
        if (g_gaunt[0].built || g_gaunt[1].built) GauntletKill();
        return;
    }
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    bool thrust[2] = {in.x, in.a};
    for (int h = 0; h < 2; h++) {
        Gauntlet &g = g_gaunt[h];
        if (!g.built) GauntletBuild(h, hands[h]);
        if (!g.built) continue;
        float s = kPalmSide[h];
        for (int i = 0; i < 3; i++) {
            Il2CppObject *f = g.flame[i].get();
            if (!Alive(f)) continue;
            if (!thrust[h]) { if (g.lit) G.goSetActive[f](false); continue; }
            if (!g.lit) G.goSetActive[f](true);
            float len = (0.08f + 0.10f * Rand01()) * (1.0f - 0.25f * i);     // flickers every frame
            float w = 0.045f - 0.01f * i;
            Il2CppObject *t = Tr(f);
            G.trSetLocalScale[t](Vector3(len, w, w));
            G.trSetLocalPosition[t](Vector3(s * (0.05f + len * 0.5f), 0.0f, 0.03f));
        }
        g.lit = thrust[h];
    }
    if ((thrust[0] || thrust[1]) && G.haptic.IsValid() && g_time >= g_nextHaptic) {
        g_nextHaptic = g_time + 0.05;
        for (int h = 0; h < 2; h++) {
            if (!thrust[h]) continue;
            XRDevice d = G.deviceAtNode(h ? 5 : 4);
            if (d.initialized) G.haptic(d.id, 0u, 0.30f + 0.30f * Rand01(), 0.08f);
        }
    }
}

// ---- Objects: drawing and placed shapes (only you see them; nothing is sent) -----------
struct Stroke { Ref go, line; int n = 0; };
static std::vector<Stroke> g_strokes;
static std::vector<size_t> g_sessions;           // where each press of the trigger began, for Undo
static std::vector<Ref> g_placed;
static int g_drawColor = 0, g_drawSize = 1, g_totalPts = 0;
static bool g_drawing = false;
static Vector3 g_lastPt;
static float g_hue = 0;
static const struct { const char *name; Color c; } kDrawColors[] = {
    {"Red", Color(1.0f, 0.10f, 0.10f, 1)},   {"Orange", Color(1.0f, 0.50f, 0.0f, 1)}, {"Yellow", Color(1.0f, 0.90f, 0.10f, 1)},
    {"Green", Color(0.15f, 0.90f, 0.25f, 1)}, {"Cyan", Color(0.10f, 0.90f, 1.0f, 1)},  {"Blue", Color(0.15f, 0.30f, 1.0f, 1)},
    {"Purple", Color(0.65f, 0.25f, 1.0f, 1)}, {"White", Color(1, 1, 1, 1)},            {"Rainbow", Color(1, 1, 1, 1)}};
static constexpr int kDrawColorCount = sizeof(kDrawColors) / sizeof(kDrawColors[0]);
static const struct { const char *name; float w; } kDrawSizes[] = {{"Thin", 0.01f}, {"Medium", 0.025f}, {"Thick", 0.05f}, {"Huge", 0.1f}};
static constexpr size_t kMaxStrokes = 150, kMaxPlaced = 40;
static constexpr int kMaxPoints = 6000, kStrokePoints = 300, kRainbowPoints = 12;

static bool RainbowSelected() { return g_drawColor == kDrawColorCount - 1; }
static Color DrawColor() { return RainbowSelected() ? Rainbow(g_hue) : kDrawColors[g_drawColor].c; }

static void KillStroke(Stroke &st) { Kill(st.go); st.line.clear(); }
static void DropOldestStroke() {
    if (g_strokes.empty()) return;
    g_totalPts -= g_strokes.front().n;
    KillStroke(g_strokes.front());
    g_strokes.erase(g_strokes.begin());
    for (auto &ss : g_sessions) if (ss > 0) ss--;
}

static bool NewStroke(Color c, Vector3 first) {
    if (!G.lrSetPosition.IsValid() || !G.lrSetPositionCount.IsValid() || !G.tLine) return false;
    while (!g_strokes.empty() && (g_strokes.size() >= kMaxStrokes || g_totalPts > kMaxPoints)) DropOldestStroke();
    static Class goClass("UnityEngine", "GameObject");
    Il2CppObject *go = goClass.CreateNewObjectParameters(CreateMonoString("LinearDraw"));
    Il2CppObject *line = go ? G.goAddComponent[go](G.tLine) : nullptr;
    if (!go || !line) {
        if (go) G.destroy(go);
        return false;
    }
    if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
        if (G.shader.get()) G.matSetShader[m](G.shader.get());
        G.matSetColor[m](c);
    }
    float w = kDrawSizes[g_drawSize].w;
    G.lrSetStartWidth[line](w);
    G.lrSetEndWidth[line](w);
    G.lrSetUseWorldSpace[line](true);
    G.lrSetPositionCount[line](1);
    G.lrSetPosition[line](0, first);
    Stroke st;
    st.go.set(go);
    st.line.set(line);
    st.n = 1;
    g_strokes.push_back(st);
    g_totalPts++;
    return true;
}

// Hold the right trigger to draw at your fingertip. Skipped while the menu is open, and
// whenever another mod already uses the right trigger.
static void DrawTick(const Buttons &in) {
    Il2CppObject *hand = g_rightHand.get();
    bool want = f_draw && in.rtrig && hand && !g_menuShown && !f_noclip && !f_spawnOnHand &&
                !((f_tpGun || f_itemGun) && in.rgrip);
    if (!want) { g_drawing = false; return; }
    Vector3 p = G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.12f;
    if (!g_drawing) {
        if (!NewStroke(DrawColor(), p)) return;
        g_drawing = true;
        g_sessions.push_back(g_strokes.size() - 1);
        g_lastPt = p;
        return;
    }
    Vector3 d = p - g_lastPt;
    if (d.x * d.x + d.y * d.y + d.z * d.z < 0.015f * 0.015f) return;      // add a point every 1.5 cm
    if (g_strokes.empty() || !Alive(g_strokes.back().line.get())) { g_drawing = false; return; }
    Stroke *st = &g_strokes.back();
    if (st->n >= kStrokePoints || (RainbowSelected() && st->n >= kRainbowPoints)) {
        g_hue += 0.04f;                                                    // next piece starts where this one ended
        if (!NewStroke(DrawColor(), g_lastPt)) { g_drawing = false; return; }
        st = &g_strokes.back();
    }
    G.lrSetPositionCount[st->line.get()](st->n + 1);
    G.lrSetPosition[st->line.get()](st->n, p);
    st->n++;
    g_totalPts++;
    g_lastPt = p;
}

static void NextDrawColor() { g_drawColor = (g_drawColor + 1) % kDrawColorCount; Notify("Draw color: %s", kDrawColors[g_drawColor].name); }
static void NextDrawSize() { g_drawSize = (g_drawSize + 1) % 4; Notify("Draw size: %s", kDrawSizes[g_drawSize].name); }

static void UndoDrawing() {
    if (g_sessions.empty()) { Notify("Nothing to undo"); return; }
    size_t from = g_sessions.back();
    g_sessions.pop_back();
    while (g_strokes.size() > from) {
        g_totalPts -= g_strokes.back().n;
        KillStroke(g_strokes.back());
        g_strokes.pop_back();
    }
    Notify("Drawing undone");
}

static void ObjectsReset() {
    for (auto &st : g_strokes) KillStroke(st);
    g_strokes.clear();
    g_sessions.clear();
    for (auto &r : g_placed) Kill(r);
    g_placed.clear();
    g_totalPts = 0;
    g_drawing = false;
}
static void ClearObjects() { ObjectsReset(); Notify("Objects cleared"); }

// A shape in front of you, in the draw color and size. No collider: you can't touch it.
static void PlaceShape(int type) {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    if (g_placed.size() >= kMaxPlaced) { Kill(g_placed.front()); g_placed.erase(g_placed.begin()); }
    Il2CppObject *o = Prim(type, DrawColor(), false);
    if (!o) return;
    float sc = kDrawSizes[g_drawSize].w * 6.0f;                           // 6 cm .. 60 cm
    Il2CppObject *t = Tr(o);
    G.trSetPosition[t](G.trGetPosition[head]() + G.trGetForward[head]() * 0.8f);
    G.trSetLocalScale[t](Vector3(sc, sc, sc));
    Ref r;
    r.set(o);
    g_placed.push_back(r);
    Notify("%s placed", type == 3 ? "Cube" : "Sphere");
}
static void PlaceCube() { PlaceShape(3); }
static void PlaceSphere() { PlaceShape(0); }

// ---- Skin Color and Eye Color (local only) --------------------------------------------
// There's no name lookup available here to find "the eye renderer" by name, so this
// grabs every Renderer under your own rig the same way Noclip finds every collider,
// and recolors all of them for Skin Color. Eye Color instead recolors just ONE of
// them, picked by kEyeMatSlot below, as a best guess at which slot the eyes are. If it
// tints the wrong part of you, change kEyeMatSlot to another small number and rebuild;
// there's no way to know the right one without testing it in the game.
struct BodyMat { Ref mat; Color orig; bool hasOrig = false; };
static std::vector<BodyMat> g_bodyMats;
static bool g_bodyMatsBuilt = false;
static int g_skinIdx = 0, g_eyeIdx = 0;                // 0 = default/off
static constexpr int kEyeMatSlot = 1;                  // which renderer is probably the eyes; see above
static const struct { const char *name; Color c; } kBodyColors[] = {
    {"Default", Color(1, 1, 1, 1)}, {"Red", Color(0.85f, 0.10f, 0.10f, 1)}, {"Orange", Color(1.0f, 0.50f, 0.0f, 1)},
    {"Yellow", Color(1.0f, 0.85f, 0.10f, 1)}, {"Green", Color(0.15f, 0.80f, 0.25f, 1)}, {"Cyan", Color(0.10f, 0.85f, 0.95f, 1)},
    {"Blue", Color(0.15f, 0.30f, 1.0f, 1)}, {"Purple", Color(0.60f, 0.20f, 0.95f, 1)}, {"Black", Color(0.05f, 0.05f, 0.05f, 1)}};
static constexpr int kBodyColorCount = sizeof(kBodyColors) / sizeof(kBodyColors[0]);

static void CollectBodyMats() {
    g_bodyMats.clear();
    g_bodyMatsBuilt = true;
    Il2CppObject *player = g_player.get();
    if (!player || !G.bodyCollider.IsValid() || !G.getComponentsInChildren.IsValid()) return;
    Il2CppObject *body = G.bodyCollider[player]();
    if (!body) return;
    Il2CppObject *rig = G.compGameObject[G.trGetRoot[G.compTransform[body]()]()]();
    if (!rig) return;
    auto *arr = (uint8_t *)G.getComponentsInChildren[rig]((Il2CppObject *)G.tRenderer, false);
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n && g_bodyMats.size() < 24; i++) {
        Il2CppObject *r = ((Il2CppObject **)(arr + 0x20))[i];
        Il2CppObject *m = r && G.rendererGetMaterial.IsValid() ? G.rendererGetMaterial[r]() : nullptr;
        if (!m) continue;
        BodyMat bm;
        bm.mat.set(m);
        if (G.matGetColor.IsValid()) { bm.orig = G.matGetColor[m](); bm.hasOrig = true; }
        g_bodyMats.push_back(bm);
    }
}

// Reapplied every frame: the game may rebuild materials on its own, so a one-time set
// wouldn't stick.
static void SkinEyeTick() {
    if (!g_player.get()) return;
    if (!g_bodyMatsBuilt) CollectBodyMats();
    if (g_bodyMats.empty()) return;
    for (size_t i = 0; i < g_bodyMats.size(); i++) {
        Il2CppObject *m = g_bodyMats[i].mat.get();
        if (!Alive(m)) continue;
        if ((int)i == kEyeMatSlot && g_eyeIdx > 0) { G.matSetColor[m](kBodyColors[g_eyeIdx].c); continue; }
        if (g_skinIdx > 0) { G.matSetColor[m](kBodyColors[g_skinIdx].c); continue; }
        if (g_bodyMats[i].hasOrig) G.matSetColor[m](g_bodyMats[i].orig);   // back to default
    }
}
static void NextSkinColor() { g_skinIdx = (g_skinIdx + 1) % kBodyColorCount; Notify("Skin: %s", kBodyColors[g_skinIdx].name); }
static void NextEyeColor() { g_eyeIdx = (g_eyeIdx + 1) % kBodyColorCount; Notify("Eyes: %s", kBodyColors[g_eyeIdx].name); }

// ---- FPS/Clock on-screen HUD (local only) ----------------------------------------------
// A small label parented to your head, in the corner of view, shown whenever the menu
// is closed too. "Show FPS"/"Show Clock" still choose what it displays; this only
// chooses where: the HUD when on, the menu title when off.
static Ref g_hudGo, g_hudText;
static double g_nextHud = 0;
static void HudTick() {
    Il2CppObject *head = g_head.get();
    bool want = f_hud && (f_fps || f_clock) && head;
    if (!want) { Kill(g_hudGo); g_hudText.clear(); return; }
    if (!Alive(g_hudGo.get())) {
        if (Il2CppObject *canvas = Canvas(head, Vector3(0.11f, 0.08f, 0.35f), 0.12f, 0.03f)) {
            G.trSetLocalRotation[Tr(canvas)](Quaternion(0, 0, 0, 1));     // rides along with your head
            Label(Tr(canvas), "", Vector3(0, 0, 0), 0.12f, 0.03f, 14, &g_hudText);
            g_hudGo.set(canvas);
        }
        g_nextHud = 0;
    }
    if (g_time < g_nextHud) return;
    g_nextHud = g_time + 0.1;
    char buf[96];
    int n = 0;
    if (f_fps) n += snprintf(buf + n, sizeof buf - n, "%d fps", (int)(g_fps + 0.5f));
    if (f_clock) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        n += snprintf(buf + n, sizeof buf - n, "%s%d:%02d", n ? "  " : "", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    }
    SetText(g_hudText, buf);
}

// ---- Leave Yeeps: quits the app -------------------------------------------------------
static void QuitGame() {
    static Method<void> quit;
    static bool looked = false;
    if (!looked) {
        looked = true;
        Class app("UnityEngine", "Application");
        if (app.IsValid()) quit = app.GetMethod("Quit", 0);
    }
    if (quit.IsValid()) { ym::log("linear-client: Leave Yeeps pressed"); quit(); }
    else Notify("Can't quit from here");
}


// ---- Linear Client additions (they only change your own game) ----------------

// Air Brake: hold X to stop dead. Iron Man uses X and A together, so A wins.
static void AirBrakeTick(const Buttons &in) {
    if (!f_airBrake || !in.x || in.a) return;
    StopMoving();
}

// Jetpack: hold B to rise, up to a top speed. B opens the menu with Right Hand Menu on.
static void JetpackTick(const Buttons &in, float dt) {
    if (!f_jetpack || f_rightHand || !in.b) return;
    Vector3 v;
    if (!GetVel(v)) return;
    float up = v.y + 25.0f * dt;
    if (up > 7.0f) up = v.y > 7.0f ? v.y : 7.0f;       // never slows a faster rise
    SetVel(Vector3(v.x, up, v.z));
}

// No Fog: RenderSettings is global, so the old value is kept and put back when you
// switch it off. Checked every frame so it also holds after the scene changes.
static bool g_fogApplied = false, g_fogOld = true;
// Long Arms: stretches each hand out from your shoulder, toward where your
// controller actually points, every frame. Nothing is sent, so other players still
// see your normal-length arms.
static constexpr float kArmScale = 3.0f;
static void LongArmsTick() {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    if (!f_longArms || !head || !lh || !rh) return;
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), up(0, 1, 0);
    Vector3 r = Cross(up, f);
    Vector3 shoulder = hp + up * -0.25f;
    Vector3 shL = shoulder - r * 0.18f, shR = shoulder + r * 0.18f;
    Vector3 pl = G.trGetPosition[lh](), pr = G.trGetPosition[rh]();
    G.trSetPosition[lh](shL + (pl - shL) * kArmScale);
    G.trSetPosition[rh](shR + (pr - shR) * kArmScale);
}

static void FogTick() {
    static Method<void> setFog;
    static Method<bool> getFog;
    static bool looked = false;
    if (!looked) {
        looked = true;
        Class rs("UnityEngine", "RenderSettings");
        if (rs.IsValid()) { setFog = rs.GetMethod("set_fog", 1); getFog = rs.GetMethod("get_fog", 0); }
    }
    if (!setFog.IsValid() || !getFog.IsValid()) return;
    if (f_noFog) {
        if (!g_fogApplied) { g_fogOld = getFog(); g_fogApplied = true; }
        if (getFog()) setFog(false);
    } else if (g_fogApplied) {
        setFog(g_fogOld);
        g_fogApplied = false;
    }
}

// ---- callbacks ---------------------------------------------------------------

static void OnRoomChange() {
    // The old room's objects are gone (or about to be): forget them, build again.
    for (int r = 0; r < kRows; r++) { RRClear(g_rowCube[r]); g_rowLabel[r].clear(); g_rowText[r].clear(); }
    for (RRect *rr : {&g_bg, &g_outline, &g_titleBar, &g_prev, &g_next, &g_backPlate, &g_backBadge, &g_backBar[0], &g_backBar[1]}) RRClear(*rr);
    for (Ref *r : {&g_board, &g_title, &g_notifyText, &g_tipText}) r->clear();
    for (Ref *r : {&g_menu, &g_pointer, &g_notifyGo, &g_leftPlatform, &g_rightPlatform, &g_gunBall, &g_gunLineGo, &g_hudGo}) Kill(*r);
    g_hudText.clear();
    g_bodyMats.clear();
    g_bodyMatsBuilt = false;
    PauseTrackers(false);
    GauntletKill();
    ObjectsReset();
    g_dancing = false;
    g_gunLine.clear();
    if (g_noclipOn && g_player.get()) {           // leaving the room mid-noclip: put things back
        Buttons none;                             // trigger "released"
        NoclipTick(none);
    }
    g_boosted = nullptr;
    g_gravityOff = false;
    for (Ref *r : {&g_player, &g_rigidbody, &g_leftHand, &g_rightHand, &g_head}) r->clear();
    g_built = false; g_menuShown = false; g_touching = -2; g_haveSpot = false;
}

static void OnUpdate(float dt) {
    g_time += dt;
    g_frames++;
    static double fpsStart = 0;
    if (g_time - fpsStart >= 1.0) { g_fps = (float)(g_frames / (g_time - fpsStart)); g_frames = 0; fpsStart = g_time; }
    if (!Lookups()) return;
    auto *player = (Il2CppObject *)ym::api()->localPlayer();
    if (!player) return;
    if (player != g_player.get()) {                 // first frame in this room
        OnRoomChange();
        g_player.set(player);
        g_leftHand.set(G.leftHand[player]());
        g_rightHand.set(G.rightHand[player]());
        g_head.set(G.headTransform[player]());
        Il2CppObject *pgo = G.compGameObject[player]();
        if (pgo) g_rigidbody.set(G.goGetComponent[pgo](G.tRigidbody));
    }
    if (!g_leftHand.get() || !g_rightHand.get() || !g_head.get()) return;
    if (!g_built) BuildMenu();

    Buttons in = ReadButtons();
    ShowMenu(f_rightHand ? in.b : in.y);
    if (g_menuShown) {
        PlaceMenu();
        CheckPress();
        if (g_dirty) RenderPage();
        UpdateTitle();
        AnimateMenu();
        OpenBuzzTick();
    }
    if (g_notifyUntil > 0 && g_time >= g_notifyUntil) { Show(g_notifyGo, false); g_notifyUntil = 0; }
    FlyTick(in, dt);
    LowGravityTick(in, dt);
    PlatformFor(in.lgrip, g_leftPlatform, g_leftHand);
    PlatformFor(in.rgrip, g_rightPlatform, g_rightHand);
    VelocityFlyTick(in);
    CarTick(in);
    IronManTick(in, dt);
    IronGauntletTick(in);
    ApplySpeedBoost();
    ApplyGravity();
    NoclipTick(in);
    AirBrakeTick(in);
    JetpackTick(in, dt);
    LongArmsTick();
    SkinEyeTick();
    HudTick();
    FogTick();
    TeleportGunTick(in);
    try { SpawnOnHandTick(in); ItemQueueTick(); }
    catch (...) { g_itemQueue.clear(); ym::log("linear-client: item code error"); }
    DrawTick(in);
    DanceTick(in, dt);              // last: it moves your hands after everything else
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    ym::log("Linear Client loaded on %s (%s)", api->gameVersion, api->buildId);
    return 0;
}

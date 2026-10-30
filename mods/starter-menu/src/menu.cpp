// Starter Menu: a small hand menu with only local, safe features.
//   Hold Y (left hand)      -> menu shows next to your left hand
//   Touch a button with your right index finger to press it
//   Fly:        hold A to fly where you look
//   Platforms:  hold a grip to stand on a platform under that hand
// Nothing here is sent to other players. Read docs/GUIDE.md before adding to it.
#include "ymsdk.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace BNM;
using namespace BNM::Structures::Unity;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("Starter Menu")

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

// ---- game lookups, resolved once -------------------------------------------

struct XRDevice { uint64_t id; bool initialized; };
static struct {
    bool ok = false;
    Method<Il2CppObject *> createPrimitive, goTransform, goGetComponent, goAddComponent, compTransform, compGameObject;
    Method<void> goSetActive, destroy, trSetParent, trSetPosition, trSetRotation, trSetLocalPosition,
                 trSetLocalRotation, trSetLocalScale, rbSetVelocity, matSetColor, matSetShader,
                 textSetFont, textSetText, textSetFontSize, textSetAlignment, graphicSetColor,
                 canvasSetRenderMode, rtSetSizeDelta;
    Method<Vector3> trGetPosition, trGetForward, trInverseTransformPoint, trTransformPoint;
    Method<bool> alive, tryGetBool;
    Method<XRDevice> deviceAtNode;
    Method<Il2CppObject *> rendererGetMaterial, shaderFind, findAll;
    Field<Il2CppObject *> leftHand, rightHand, headTransform;
    BNM::MonoType *tRigidbody = nullptr, *tCollider = nullptr, *tCanvas = nullptr, *tText = nullptr,
                  *tFont = nullptr, *tRenderer = nullptr;
    Ref shader, font, usagePrimary, usageSecondary, usageGrip;
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
    if (!go || !tr || !player || !dev) { ym::log("starter-menu: game classes missing"); return false; }
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
    G.trGetForward = tr.GetMethod("get_forward", 0);
    G.trInverseTransformPoint = Overload(tr, "InverseTransformPoint", {"UnityEngine.Vector3"});
    G.trTransformPoint = Overload(tr, "TransformPoint", {"UnityEngine.Vector3"});
    G.rbSetVelocity = rb.GetMethod("set_velocity", 1);
    G.rendererGetMaterial = rend.GetMethod("get_material", 0);
    G.matSetColor = Overload(mat, "set_color", {"UnityEngine.Color"});
    G.matSetShader = mat.GetMethod("set_shader", 1);
    G.shaderFind = sh.GetMethod("Find", 1);
    G.findAll = Overload(res, "FindObjectsOfTypeAll", {"System.Type"});
    G.textSetFont = text.GetMethod("set_font", 1);
    G.textSetText = text.GetMethod("set_text", 1);
    G.textSetFontSize = text.GetMethod("set_fontSize", 1);
    G.textSetAlignment = text.GetMethod("set_alignment", 1);
    G.graphicSetColor = graphic.GetMethod("set_color", 1);
    G.canvasSetRenderMode = canvas.GetMethod("set_renderMode", 1);
    G.rtSetSizeDelta = rt.GetMethod("set_sizeDelta", 1);
    G.deviceAtNode = dev.GetMethod("GetDeviceAtXRNode", 1);
    G.tryGetBool = dev.GetMethod("TryGetFeatureValue_bool", 3);
    G.leftHand = player.GetField("leftHandTransform");
    G.rightHand = player.GetField("rightHandTransform");
    G.headTransform = player.GetField("headTransform");
    G.tRigidbody = rb.GetMonoType();
    G.tCollider = Class("UnityEngine", "Collider").GetMonoType();
    G.tRenderer = rend.GetMonoType();
    G.tCanvas = canvas.GetMonoType();
    G.tText = text.GetMonoType();
    G.tFont = Class("UnityEngine", "Font").GetMonoType();
    G.usagePrimary.set((Il2CppObject *)CreateMonoString("PrimaryButton"));
    G.usageSecondary.set((Il2CppObject *)CreateMonoString("SecondaryButton"));
    G.usageGrip.set((Il2CppObject *)CreateMonoString("GripButton"));
    for (const char *s : {"Universal Render Pipeline/Unlit", "Universal Render Pipeline/Lit"})
        if (!G.shader.get() && G.shaderFind.IsValid()) G.shader.set(G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(s)));
    if (G.findAll.IsValid() && G.tFont) {            // any font the game already loaded
        auto *arr = (uint8_t *)G.findAll((Il2CppObject *)G.tFont);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) G.font.set(((Il2CppObject **)(arr + 0x20))[0]);
    }
    G.ok = G.createPrimitive.IsValid() && G.goTransform.IsValid() && G.trSetPosition.IsValid() &&
           G.deviceAtNode.IsValid() && G.tryGetBool.IsValid() && G.leftHand.IsValid();
    if (!G.ok) ym::log("starter-menu: some game methods are missing, menu off");
    return G.ok;
}

static bool Alive(Il2CppObject *o) { return o && G.alive((Il2CppObject *)o); }
static Il2CppObject *Tr(Il2CppObject *gameObject) { return G.goTransform[gameObject](); }

// A cube with a flat color. collider=false removes its collider so it can't push you.
static Il2CppObject *Cube(Color c, bool collider) {
    Il2CppObject *o = G.createPrimitive(3);   // PrimitiveType.Cube
    if (!o) return nullptr;
    if (!collider) if (auto *col = G.goGetComponent[o](G.tCollider)) G.destroy(col);
    if (auto *r = G.goGetComponent[o](G.tRenderer)) {
        Il2CppObject *m = G.rendererGetMaterial[r]();
        if (m && G.shader.get()) G.matSetShader[m](G.shader.get());
        if (m) G.matSetColor[m](c);
    }
    return o;
}
static void SetColor(Il2CppObject *o, Color c) {
    if (!Alive(o)) return;
    if (auto *r = G.goGetComponent[o](G.tRenderer)) if (Il2CppObject *m = G.rendererGetMaterial[r]()) G.matSetColor[m](c);
}

// ---- input -------------------------------------------------------------------

struct Buttons { bool a = false, b = false, x = false, y = false, lgrip = false, rgrip = false; };
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
    return b;
}

// ---- the menu ----------------------------------------------------------------

struct Button {
    const char *label;
    bool toggle;
    bool on = false;
    void (*onPress)() = nullptr;     // for non-toggle buttons
    Ref cube;
};
static void NextColor();
static Button g_buttons[] = {
    {"Fly (hold A)", true},
    {"Platforms (grips)", true},
    {"Menu color", false, false, NextColor},
};
static constexpr int kButtons = sizeof(g_buttons) / sizeof(g_buttons[0]);
static Button &Fly() { return g_buttons[0]; }
static Button &Platforms() { return g_buttons[1]; }

static const Color kThemes[][2] = {   // {button, button pressed}
    {Color(1.0f, 0.55f, 0.1f, 1), Color(0.45f, 0.2f, 0.0f, 1)},
    {Color(0.3f, 0.6f, 1.0f, 1), Color(0.1f, 0.2f, 0.5f, 1)},
    {Color(0.6f, 0.9f, 0.4f, 1), Color(0.2f, 0.4f, 0.1f, 1)},
};
static int g_theme = 0;

static Ref g_player, g_rigidbody, g_leftHand, g_rightHand, g_head;
static Ref g_menu, g_pointer, g_leftPlatform, g_rightPlatform;
static bool g_built = false, g_menuShown = false;
static double g_time = 0, g_nextPress = 0;
static int g_touching = -1;

static const float kW = 0.26f, kRowH = 0.045f, kTop = 0.07f;   // menu size in meters

static void PaintButtons() {
    for (auto &b : g_buttons) SetColor(b.cube.get(), (b.toggle && b.on) ? kThemes[g_theme][1] : kThemes[g_theme][0]);
}
static void NextColor() { g_theme = (g_theme + 1) % 3; PaintButtons(); }

static Il2CppObject *Label(Il2CppObject *canvasT, const char *s, float y) {
    if (!G.font.get() || !G.tText) return nullptr;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("Label"));
    Il2CppObject *text = go ? G.goAddComponent[go](G.tText) : nullptr;
    if (!text) return nullptr;
    Il2CppObject *t = Tr(go);
    G.trSetParent[t](canvasT, false);
    G.rtSetSizeDelta[t](Vector2(kW * 1000, kRowH * 1000));
    G.trSetLocalPosition[t](Vector3(0, y * 1000, 0));
    G.trSetLocalRotation[t](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[t](Vector3(1, 1, 1));
    G.textSetFont[text](G.font.get());
    G.textSetText[text](CreateMonoString(s));
    G.textSetFontSize[text](26);
    G.textSetAlignment[text](4);                 // MiddleCenter
    G.graphicSetColor[text](Color(1, 1, 1, 1));
    return go;
}

// Built once per room; afterwards only shown/hidden and moved.
static void BuildMenu() {
    Il2CppObject *menu = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("StarterMenu"));
    if (!menu) return;
    g_menu.set(menu);
    Il2CppObject *menuT = Tr(menu);
    float height = kButtons * kRowH + 0.06f;
    if (Il2CppObject *bg = Cube(Color(0.08f, 0.08f, 0.1f, 1), false)) {
        Il2CppObject *t = Tr(bg);
        G.trSetParent[t](menuT, false);
        G.trSetLocalPosition[t](Vector3(0, kTop - height / 2 + 0.03f, 0.006f));
        G.trSetLocalScale[t](Vector3(kW + 0.02f, height, 0.004f));
    }
    for (int i = 0; i < kButtons; i++) {
        Il2CppObject *c = Cube(kThemes[g_theme][0], false);
        if (!c) continue;
        Il2CppObject *t = Tr(c);
        G.trSetParent[t](menuT, false);
        G.trSetLocalPosition[t](Vector3(0, kTop - (i + 1) * kRowH, 0));
        G.trSetLocalScale[t](Vector3(kW, kRowH * 0.8f, 0.01f));
        g_buttons[i].cube.set(c);
    }
    // World-space canvas for the text, 1 unit = 1 mm
    Il2CppObject *canvasGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("StarterMenuText"));
    Il2CppObject *canvas = canvasGo && G.tCanvas ? G.goAddComponent[canvasGo](G.tCanvas) : nullptr;
    if (canvas) {
        G.canvasSetRenderMode[canvas](2);        // WorldSpace
        Il2CppObject *ct = Tr(canvasGo);
        G.trSetParent[ct](menuT, false);
        G.rtSetSizeDelta[ct](Vector2(kW * 1000, height * 1000));
        G.trSetLocalPosition[ct](Vector3(0, 0, -0.006f));
        G.trSetLocalRotation[ct](Quaternion(0, 0, 0, 1));
        G.trSetLocalScale[ct](Vector3(0.001f, 0.001f, 0.001f));
        Label(ct, "Starter Menu", kTop);
        for (int i = 0; i < kButtons; i++) Label(ct, g_buttons[i].label, kTop - (i + 1) * kRowH);
    }
    // Pointer: a small sphere on the right index finger
    if (Il2CppObject *p = G.createPrimitive(0)) {            // Sphere
        if (auto *col = G.goGetComponent[p](G.tCollider)) G.destroy(col);
        Il2CppObject *t = Tr(p);
        G.trSetParent[t](g_rightHand.get(), false);
        G.trSetLocalPosition[t](Vector3(0.01f, -0.017f, 0.19f));
        G.trSetLocalScale[t](Vector3(0.012f, 0.012f, 0.012f));
        g_pointer.set(p);
    }
    PaintButtons();
    G.goSetActive[menu](false);
    if (g_pointer.get()) G.goSetActive[g_pointer.get()](false);
    g_built = true;
}

static void ShowMenu(bool show) {
    if (show == g_menuShown) return;
    g_menuShown = show;
    if (Alive(g_menu.get())) G.goSetActive[g_menu.get()](show);
    if (Alive(g_pointer.get())) G.goSetActive[g_pointer.get()](show);
}

static void PlaceMenu() {
    Il2CppObject *menu = g_menu.get(), *hand = g_leftHand.get(), *head = g_head.get();
    if (!Alive(menu) || !hand || !head) return;
    Vector3 handPos = G.trGetPosition[hand](), headPos = G.trGetPosition[head]();
    Vector3 pos = handPos + Vector3(0, 0.12f, 0);
    Vector3 away = pos - headPos;                  // menu faces the player
    if (away.x * away.x + away.y * away.y + away.z * away.z < 1e-6f) return;
    Il2CppObject *t = Tr(menu);
    G.trSetPosition[t](pos);
    G.trSetRotation[t](Quaternion::LookRotation(away));
}

static void CheckPress() {
    Il2CppObject *menu = g_menu.get(), *pointer = g_pointer.get();
    if (!Alive(menu) || !Alive(pointer)) return;
    Vector3 p = G.trInverseTransformPoint[Tr(menu)](G.trGetPosition[Tr(pointer)]());
    int hit = -1;
    for (int i = 0; i < kButtons; i++) {
        float by = kTop - (i + 1) * kRowH;
        if (p.x > -kW / 2 && p.x < kW / 2 && p.y > by - kRowH * 0.4f && p.y < by + kRowH * 0.4f && p.z > -0.04f && p.z < 0.02f) hit = i;
    }
    if (hit >= 0 && hit != g_touching && g_time >= g_nextPress) {   // one press per touch
        g_nextPress = g_time + 0.25;
        Button &b = g_buttons[hit];
        if (b.toggle) b.on = !b.on; else if (b.onPress) b.onPress();
        PaintButtons();
        ym::log("starter-menu: %s %s", b.label, b.toggle ? (b.on ? "on" : "off") : "pressed");
    }
    g_touching = hit;
}

// ---- features (local only) -----------------------------------------------------

static void FlyTick(const Buttons &in, float dt) {
    Il2CppObject *player = g_player.get(), *head = g_head.get();
    if (!Fly().on || !in.a || !player || !head) return;
    Il2CppObject *t = G.compTransform[player]();
    if (!t) return;
    Vector3 pos = G.trGetPosition[t]() + G.trGetForward[head]() * (8.0f * dt);
    G.trSetPosition[t](pos);
    if (g_rigidbody.get() && G.rbSetVelocity.IsValid()) G.rbSetVelocity[g_rigidbody.get()](Vector3(0, 0, 0));
}

static void PlatformFor(bool held, Ref &platform, Ref &hand) {
    if (!held || !Platforms().on) {
        if (Alive(platform.get())) G.destroy(platform.get());
        platform.clear();
        return;
    }
    if (platform.get() || !hand.get()) return;              // already standing on one
    Il2CppObject *p = Cube(kThemes[g_theme][0], true);      // keeps its collider: you stand on it
    if (!p) return;
    Il2CppObject *t = Tr(p);
    G.trSetPosition[t](G.trGetPosition[hand.get()]() - Vector3(0, 0.08f, 0));
    G.trSetLocalScale[t](Vector3(0.28f, 0.02f, 0.28f));
    platform.set(p);
}

// ---- callbacks ---------------------------------------------------------------

static void OnRoomChange() {
    // The old room's objects are gone (or about to be): forget them, build again.
    for (auto &b : g_buttons) b.cube.clear();
    for (Ref *r : {&g_menu, &g_pointer, &g_leftPlatform, &g_rightPlatform}) {
        if (Alive(r->get())) G.destroy(r->get());
        r->clear();
    }
    for (Ref *r : {&g_player, &g_rigidbody, &g_leftHand, &g_rightHand, &g_head}) r->clear();
    g_built = false; g_menuShown = false; g_touching = -1;
}

static void OnUpdate(float dt) {
    g_time += dt;
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
    if (!g_leftHand.get() || !g_rightHand.get()) return;
    if (!g_built) BuildMenu();

    Buttons in = ReadButtons();
    ShowMenu(in.y);
    if (g_menuShown) { PlaceMenu(); CheckPress(); }
    FlyTick(in, dt);
    PlatformFor(in.lgrip, g_leftPlatform, g_leftHand);
    PlatformFor(in.rgrip, g_rightPlatform, g_rightHand);
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    ym::log("starter-menu loaded on %s (%s)", api->gameVersion, api->buildId);
    return 0;
}

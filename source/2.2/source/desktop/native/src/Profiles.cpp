#include "PCH.h"
#include "Profiles.h"
#include "BottleShapeName.h"
#include "UserSettings.h"

namespace nuka {
using Json=nlohmann::json;
struct MeshIdentity {float matrix[16]{};float rings[16][2]{};unsigned ringCount=0;};
static void Keys(const Json& j,std::initializer_list<const char*> allowed){
    if(!j.is_object())throw std::runtime_error("Expected a configuration object");
    for(auto it=j.begin();it!=j.end();++it)if(std::none_of(allowed.begin(),allowed.end(),[&](const char* k){return it.key()==k;}))throw std::runtime_error("Unknown setting: "+it.key());
}
static void Controls(const Json& j){Keys(j,{"liquid_level","opacity","liquid_color","glass_thickness","bubble_density","shaking_bubbles","surface_foam","refraction_strength","reflection_strength","scattering_strength","glow_strength","glass_color","glass_opacity","wet_absorption","wet_smoothness","wet_tint","wet_refraction","wet_drying"});}
static Json Read(const std::filesystem::path& root){
    std::ifstream f(root/"drinks.json");if(!f)throw std::runtime_error("Cannot open drinks.json");auto j=Json::parse(f);
    Keys(j,{"schema_version","diagnostics","defaults","meshes"});
    if(j.at("schema_version")!=2)throw std::runtime_error("Replace the old configuration with drinks.json schema 2");
    Controls(j.at("defaults"));if(!j["defaults"].contains("reflection_strength"))j["defaults"]["reflection_strength"]=1.f;if(!j.at("meshes").is_object())throw std::runtime_error("meshes must be keyed by NIF filename");
    auto lower=[](std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;};
    std::unordered_map<std::string,std::string> names;
    for(auto it=j["meshes"].begin();it!=j["meshes"].end();++it){Controls(it.value());if(!names.emplace(lower(it.key()),it.key()).second)throw std::runtime_error("Duplicate mesh filename: "+it.key());}
    std::vector<std::filesystem::path> patches;
    if(std::filesystem::exists(root/"drinks.d"))for(const auto& patchEntry:std::filesystem::directory_iterator(root/"drinks.d"))if(patchEntry.is_regular_file()&&lower(patchEntry.path().extension().string())==".json")patches.push_back(patchEntry.path());
    std::sort(patches.begin(),patches.end());
    for(const auto& file:patches){try{
        std::ifstream input(file);auto patch=Json::parse(input);Keys(patch,{"schema_version","meshes"});
        if(patch.at("schema_version")!=1||!patch.at("meshes").is_object())throw std::runtime_error("Expected appearance patch schema 1 and meshes object");
        std::unordered_map<std::string,bool> seen;
        for(auto it=patch["meshes"].begin();it!=patch["meshes"].end();++it){Controls(it.value());auto key=lower(it.key());if(!seen.emplace(key,true).second)throw std::runtime_error("Duplicate mesh filename: "+it.key());
            auto found=names.find(key);if(found==names.end()){names.emplace(key,it.key());j["meshes"][it.key()]=it.value();}else j["meshes"][found->second].update(it.value());
        }
    }catch(const std::exception& e){throw std::runtime_error(file.filename().string()+": "+e.what());}}
    j["defaults"]["reflection_strength"]=1.f;
    for(auto& controls:j["meshes"])controls.erase("reflection_strength");
    return j;
}
static float Number(const Json& j,const char* key,float lo,float hi){
    if(!j.at(key).is_number())throw std::runtime_error(std::string("Expected number: ")+key);
    float v=j.at(key).get<float>();if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string("Out of range: ")+key);return v;
}
static V4 Color(const Json& j,const char* key="liquid_color"){
    auto color=j.at(key).get<std::string>();
    if(color.size()!=7||color[0]!='#'||color.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos)throw std::runtime_error("liquid_color must be #RRGGBB");
    auto channel=[&](int offset){float x=float(std::stoul(color.substr(offset,2),nullptr,16))/255.f;return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};
    return {channel(1),channel(3),channel(5),0};
}
static void OriginalSurfaces(const std::filesystem::path& root,DrinkMap& drinks){
    auto path=root/"surface-materials.json";
    if(!std::filesystem::exists(path))return; // Existing third-party profile packs remain valid.
    std::ifstream input(path);auto doc=Json::parse(input);
    Keys(doc,{"schema_version","materials"});
    if(doc.at("schema_version")!=1||!doc.at("materials").is_array()||doc["materials"].size()>512)
        throw std::runtime_error("Expected surface-materials.json schema 1");
    std::unordered_map<std::string,bool> seen;
    for(const auto& j:doc["materials"]){
        Keys(j,{"shape","vertices","indices","source_material","source_sha256","environment_scale","specular_colour","specular_multiplier","smoothness","fresnel_power"});
        auto shape=j.at("shape").get<std::string>();
        auto vertices=j.at("vertices").get<unsigned>(),indices=j.at("indices").get<unsigned>();
        auto signature=shape+":"+std::to_string(vertices)+":"+std::to_string(indices);
        if(!seen.emplace(signature,true).second)throw std::runtime_error("Duplicate original surface signature");
        auto colour=j.at("specular_colour").get<std::vector<float>>();
        if(colour.size()!=3)throw std::runtime_error("Expected three specular colour channels");
        for(float x:colour)if(!std::isfinite(x)||x<0||x>10)throw std::runtime_error("Invalid surface specular colour");
        V4 spec{colour[0],colour[1],colour[2],Number(j,"specular_multiplier",0,100)};
        V4 response{Number(j,"smoothness",0,1),Number(j,"fresnel_power",.001f,100),1,Number(j,"environment_scale",0,100)};
        auto found=drinks.find(shape);if(found==drinks.end())continue; // Optional patch not installed.
        auto apply=[&](DrinkProfile& d){if(d.vertices==vertices&&d.indices==indices){
            d.settings.base.surfaceSpecular=d.distant.surfaceSpecular=spec;
            d.settings.base.surfaceResponse=d.distant.surfaceResponse=response;
        }};
        apply(found->second);for(auto& variant:found->second.variants)apply(variant);
    }
}
std::string DrinkGroup(std::string mesh){
    std::transform(mesh.begin(),mesh.end(),mesh.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(mesh=="acidsoaker.nif")return "acidsoaker"; // Isolated weapon profile; do not inherit drink-group tuning.
    if(mesh.find("vim")!=std::string::npos)return "vim";
    if(mesh.find("nuka")!=std::string::npos||mesh.find("newkacola")!=std::string::npos||mesh.find("sarsaparilla")!=std::string::npos)return "cola";
    if(mesh.find("beer")!=std::string::npos||mesh=="iafanimobjectbottle.nif")return "beer";
    return "spirits";
}
std::string TuningGroup(const Json& tuning,std::string mesh){
    auto lower=[](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;};
    if(tuning.contains("memberships"))for(auto it=tuning["memberships"].begin();it!=tuning["memberships"].end();++it)
        if(lower(it.key())==lower(mesh))return it.value().get<std::string>();
    auto group=DrinkGroup(mesh);
    return tuning["groups"].contains(group)?group:std::string{};
}
void SetTuningMembership(Json& tuning,std::string mesh,const std::string& group){
    auto next=tuning;next["schema_version"]=2;
    if(!next.contains("memberships"))next["memberships"]=Json::object();
    auto lower=[](std::string v){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::tolower(c));});return v;};
    for(auto it=next["memberships"].begin();it!=next["memberships"].end();)if(lower(it.key())==lower(mesh))it=next["memberships"].erase(it);else ++it;
    next["memberships"][mesh]=group;ValidateTuning(next);tuning=std::move(next);
}
void AddTuningGroup(Json& tuning,const std::string& group,const Json& controls){
    if(tuning["groups"].contains(group))throw std::runtime_error("Group already exists");
    auto next=tuning;next["schema_version"]=2;next["groups"][group]=controls;
    if(!next.contains("memberships"))next["memberships"]=Json::object();
    ValidateTuning(next);tuning=std::move(next);
}
void RemoveTuningGroup(Json& tuning,const std::string& group){
    auto next=tuning;if(!next["groups"].contains(group))throw std::runtime_error("Group does not exist");
    next["schema_version"]=2;next["groups"].erase(group);
    if(!next.contains("memberships"))next["memberships"]=Json::object();
    for(auto& membership:next["memberships"])if(membership==group)membership="";
    ValidateTuning(next);tuning=std::move(next);
}
Json AppearanceControls(const std::filesystem::path& root){return Read(root);}
void ValidateTuning(const Json& t){
    Keys(t,{"schema_version","groups","meshes","memberships"});
    int version=t.at("schema_version").get<int>();if(version!=1&&version!=2)throw std::runtime_error("Expected tuning schema 1 or 2");
    if(version==1){Keys(t.at("groups"),{"cola","vim","beer","spirits"});if(t.contains("memberships"))throw std::runtime_error("Memberships require schema 2");}
    if(!t.at("groups").is_object())throw std::runtime_error("Expected groups object");
    std::unordered_map<std::string,bool> groupNames;
    for(auto it=t["groups"].begin();it!=t["groups"].end();++it){
        auto name=it.key();if(name.empty()||name.size()>80||name.front()==' '||name.back()==' '||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))throw std::runtime_error("Use a group name of 1-80 bytes without surrounding spaces or control characters");
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!groupNames.emplace(name,true).second)throw std::runtime_error("Duplicate group name");
    }
    if(t.contains("memberships")){
        if(!t["memberships"].is_object())throw std::runtime_error("Expected memberships object");
        std::unordered_map<std::string,bool> names;
        for(auto it=t["memberships"].begin();it!=t["memberships"].end();++it){
            auto mesh=it.key();std::transform(mesh.begin(),mesh.end(),mesh.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(mesh.empty()||!names.emplace(mesh,true).second)throw std::runtime_error("Empty or duplicate membership mesh");
            if(!it.value().is_string())throw std::runtime_error("Membership must name a group or be empty");
            auto group=it.value().get<std::string>();if(!group.empty()&&!t["groups"].contains(group))throw std::runtime_error("Membership refers to a missing group");
        }
    }
    auto validate=[](const Json& g){
        Controls(g);
        for(auto it=g.begin();it!=g.end();++it){auto k=it.key();
            if(k=="glass_color"||k=="liquid_color"){Color(g,k.c_str());continue;}
            float lo=k=="glass_thickness"?.01f:(k=="wet_drying"?.1f:0.f);
            float hi=(k=="bubble_density"||k=="glow_strength"||k=="reflection_strength")?10.f:((k=="refraction_strength"||k=="wet_drying")?20.f:(k=="glass_opacity"?.95f:1.f));Number(g,k.c_str(),lo,hi);
        }
    };
    for(auto& g:t["groups"])validate(g);
    if(!t.at("meshes").is_object())throw std::runtime_error("Expected per-mesh colors");
    std::unordered_map<std::string,bool> seen;
    for(auto it=t["meshes"].begin();it!=t["meshes"].end();++it){
        auto key=it.key();std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});if(!seen.emplace(key,true).second)throw std::runtime_error("Duplicate tuning mesh");
        validate(it.value());
    }
}
Json LoadTuning(const std::filesystem::path& root,bool factory){
    auto doc=Read(root);Json t={{"schema_version",1},{"groups",Json::object()},{"meshes",Json::object()}};
    for(auto [group,mesh]:std::array<std::pair<const char*,const char*>,4>{{{"cola","NukaColaBottleFull.nif"},{"vim","DLC03_VimBottle.nif"},{"beer","BeerBottleFull01.nif"},{"spirits","BottleRum01.nif"}}}){
        auto c=doc["defaults"];for(auto it=doc["meshes"].begin();it!=doc["meshes"].end();++it){auto a=it.key(),b=std::string(mesh);std::transform(a.begin(),a.end(),a.begin(),::tolower);std::transform(b.begin(),b.end(),b.begin(),::tolower);if(a==b)c.update(it.value());}
        for(auto key:{"liquid_level","opacity","glass_thickness","glass_opacity","bubble_density","refraction_strength"})t["groups"][group][key]=c.value(key,key==std::string("glass_opacity")?0.f:1.f);
        t["groups"][group]["shaking_bubbles"]=c.value("shaking_bubbles",1.f);
        t["groups"][group].update(Json{{"wet_absorption",0},{"wet_smoothness",.15},{"wet_refraction",.8},{"wet_drying",3.5}});
    }
    auto merge=[&](const std::filesystem::path& file,bool legacyReflection){
        if(!std::filesystem::exists(file))return;
        std::ifstream f(file);auto saved=Json::parse(f);ValidateTuning(saved);
        if(legacyReflection)for(auto section:{"groups","meshes"})for(auto& controls:saved[section])controls["reflection_strength"]=1.f;
        if(saved["schema_version"]==2){
            // Version 2 is an authoritative group catalog: deleted groups must not reappear on restart.
            t["schema_version"]=2;t["groups"]=saved["groups"];t["memberships"]=saved.value("memberships",Json::object());
        }else for(auto it=saved["groups"].begin();it!=saved["groups"].end();++it)t["groups"][it.key()].update(it.value());
        for(auto it=saved["meshes"].begin();it!=saved["meshes"].end();++it){
            auto lower=[](std::string v){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::tolower(c));});return v;};
            auto key=it.key();for(auto entry=doc["meshes"].begin();entry!=doc["meshes"].end();++entry)if(lower(entry.key())==lower(key)){key=entry.key();break;}
            // A saved mesh entry is a complete override set. An empty entry
            // deliberately restores group inheritance, including after restart.
            // Factory defaults may name a mesh absent from the selected pack.
            // Replace its existing case-insensitive identity even when Read()
            // cannot supply a canonical spelling from an active drinks file.
            for(auto existing=t["meshes"].begin();existing!=t["meshes"].end();){
                if(lower(existing.key())==lower(key))existing=t["meshes"].erase(existing);
                else ++existing;
            }
            t["meshes"][key]=it.value();
        }
    };
    merge(root/"defaults.json",true);
    for(auto& controls:t["groups"])controls["reflection_strength"]=1.f;
    if(!factory){
        auto user=UserSettingsRoot(root);
        auto current=user/"glass-reflection-test1-tuning.json";
        if(std::filesystem::exists(current))merge(current,false);
        else merge(user/"tuning.json",true);
    }
    for(auto section:{"groups","meshes"})for(auto& controls:t[section])controls.erase("scattering_strength");
    // Old builds serialized float32 UI values as long decimal expansions.
    // Six decimal places exceed the useful precision of these appearance controls.
    // Keep JSON doubles here; only Build converts values to shader floats.
    for(auto section:{"groups","meshes"})for(auto& controls:t[section])for(auto& value:controls)
        if(value.is_number_float()){double v=value.get<double>();if(std::isfinite(v))value=std::round(v*1000000.0)/1000000.0;}
    ValidateTuning(t);return t;
}
static Settings Build(const Json& controls,const MeshIdentity* identity=nullptr){
    // Internal geometry/physics for the currently supported Nuka Project family.
    // Future bottle families need their own validated cavity and transform.
    Settings s;auto& c=s.base;
    const float matrix[]={.9f,0,0,0,0,0,-.9f,9.5080078125f,0,.9f,0,.94482421875f,0,0,0,1};
    std::copy_n(matrix,16,c.matrix);
    const V4 rings[]={{.5f,2.9f},{3,3.3f},{5.35f,3.52f},{7.9f,3.51f},{10.7f,3.15f},{14.9f,2},{17.9f,1.35f},{19.9f,1.24f}};
    float wall=Number(controls,"glass_thickness",.01f,1),delta=wall-.16f;
    c.ringCount=8;for(int i=0;i<8;i++){c.rings[i]=rings[i];c.rings[i].y-=delta;if(i==0)c.rings[i].x+=delta;if(i==7)c.rings[i].x-=delta;}
    if(identity){
        std::copy_n(identity->matrix,16,c.matrix);
        c.ringCount=int(identity->ringCount);
        for(int i=0;i<c.ringCount;i++){
            c.rings[i]={identity->rings[i][0],std::max(identity->rings[i][1]-delta,.05f),0,0};
            if(i==0)c.rings[i].x+=delta;if(i==c.ringCount-1)c.rings[i].x-=delta;
        }
        
    }
    for(int i=0;i<c.ringCount;i++)c.rings[0].z=std::max(c.rings[0].z,c.rings[i].y);
    c.optics={1.5f,1.333f,wall,0};s.fill=Number(controls,"liquid_level",0,1);
    c.trace={60,s.fill,Number(controls,"refraction_strength",0,20),0};c.capZ=30;
    c.surfaceEnvironment.w=controls.contains("reflection_strength")?Number(controls,"reflection_strength",0,10):1.f;
    float opacity=Number(controls,"opacity",0,1),density=-std::log(std::max(1-opacity,.000001f))/4.f;
    c.absorption={density,density*1.5f,density*2,opacity};c.scatter=Color(controls);c.scatter.w=1; // Internal detail-distance blend; volume scattering follows opacity.
    float glow=Number(controls,"glow_strength",0,10);c.emission={c.scatter.x*glow,c.scatter.y*glow,c.scatter.z*glow,0};
    c.glassAbsorption={.012f,.006f,.002f,0};c.foamColor={.72f,.52f,.27f,0};
    if(controls.contains("glass_color")){
        auto tint=Color(controls,"glass_color");float glassOpacity=controls.contains("glass_opacity")?Number(controls,"glass_opacity",0,.95f):.45f;
        auto absorb=[&](float v){return -std::log(std::max(v,.005f)*(1-glassOpacity))/wall;};
        if(tint.x!=1||tint.y!=1||tint.z!=1||glassOpacity!=0)c.glassAbsorption={absorb(tint.x),absorb(tint.y),absorb(tint.z),glassOpacity};
    }
    s.frequency=1.6f;s.damping=.24f;s.gain=.65f;s.maxTilt=.45f;s.waveAmplitude=.065f;s.waveFrequency=1.7f;s.foamStrength=0;
    s.intensity=Number(controls,"bubble_density",0,10);s.baseline=12;s.movement=48;s.radiusMin=s.radiusMax=.05f;s.rise=1.8f;s.settle=2.5f;
    auto optional=[&](const char* key,float fallback,float lo,float hi){return controls.contains(key)?Number(controls,key,lo,hi):fallback;};
    s.shakingBubbles=optional("shaking_bubbles",0.f,0,1);
    s.foamStrength=optional("surface_foam",0.f,0,1);
    c.wetTuning={optional("wet_absorption",0.f,0,1),optional("wet_smoothness",.15f,0,1),optional("wet_refraction",.8f,0,1),optional("wet_drying",3.5f,.1f,20)};
    // Validated profile transforms have uniform orthogonal axes. Cache their
    // inverse once, so native point-light evaluation needs no extra interpolant
    // or per-draw inverse and the corrected .24 vertex shader stays intact.
    float inverseScale2=1/(c.matrix[0]*c.matrix[0]+c.matrix[1]*c.matrix[1]+c.matrix[2]*c.matrix[2]);
    for(int axis=0;axis<3;axis++){
        auto& row=c.simulationToMesh[axis];
        row={c.matrix[axis]*inverseScale2,c.matrix[4+axis]*inverseScale2,c.matrix[8+axis]*inverseScale2,0};
        row.w=-(row.x*c.matrix[3]+row.y*c.matrix[7]+row.z*c.matrix[11]);
    }
    MakeSamples(s);return s;
}
Settings LoadSettings(const std::filesystem::path& root){return Build(Read(root).at("defaults"));}
bool DiagnosticsEnabled(const std::filesystem::path& root){return Read(root).value("diagnostics",false);}
const DrinkProfile* FindProfile(const DrinkMap& drinks,std::string_view shape,unsigned vertices,unsigned indices){
    auto it=drinks.find(BottleShapeName(shape));if(it==drinks.end())return nullptr;
    auto matches=[&](const DrinkProfile& p){return p.vertices==vertices&&p.indices==indices;};
    if(matches(it->second))return &it->second;
    for(const auto& p:it->second.variants)if(matches(p))return &p;
    return nullptr;
}

// Exact source-texture identity from the applied Effect material; no filename inference.
static std::string_view TextureRelative(std::string_view p){
    auto starts=[](std::string_view p,std::string_view prefix){
        if(p.size()<prefix.size())return false;
        for(size_t i=0;i<prefix.size();++i){char c=p[i]=='\\'?'/':p[i];if(c>='A'&&c<='Z')c+=32;if(c!=prefix[i])return false;}return true;
    };
    if(starts(p,"data/"))p.remove_prefix(5);
    if(starts(p,"textures/"))p.remove_prefix(9);
    return p;
}
static bool SameTexture(std::string_view a,std::string_view b){
    a=TextureRelative(a);b=TextureRelative(b);if(a.size()!=b.size())return false;
    auto fold=[](char c){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;return c;};
    for(size_t i=0;i<a.size();++i)if(fold(a[i])!=fold(b[i]))return false;
    return true;
}
const DrinkProfile* FindMaterialProfile(const DrinkProfile& base,std::string_view sourceTexture){
    if(!sourceTexture.empty())for(const auto& [path,profile]:base.materialVariants)if(SameTexture(path,sourceTexture))return profile.get();
    return &base;
}
static void LoadMaterialRules(const std::filesystem::path& root,DrinkMap& drinks){
    auto folder=root/"materials.d";if(!std::filesystem::exists(folder))return;
    std::vector<std::filesystem::path> files;
    for(const auto& f:std::filesystem::directory_iterator(folder))if(f.is_regular_file()&&f.path().extension()==".json")files.push_back(f.path());
    std::sort(files.begin(),files.end());unsigned total=0;
    for(const auto& file:files){
        try{
            std::ifstream input(file);auto doc=Json::parse(input);Keys(doc,{"schema_version","rules"});
            if(doc.at("schema_version")!=1||!doc.at("rules").is_array())throw std::runtime_error("Expected material rules schema 1");
            for(const auto& rule:doc["rules"]){
                if(++total>256)throw std::runtime_error("Maximum 256 material rules exceeded");
                Keys(rule,{"shape","vertices","indices","source_texture","liquid","source_shape","liquid_gradient"});
                auto shape=rule.at("shape").get<std::string>();
                auto count=[&](const char* key,unsigned limit){auto& v=rule.at(key);if(!v.is_number_integer()||v.get<int64_t>()<=0||v.get<int64_t>()>limit)throw std::runtime_error("Invalid geometry count");return v.get<unsigned>();};
                auto* found=FindProfile(drinks,shape,count("vertices",65535),count("indices",3000000));
                if(!found)throw std::runtime_error("Unknown supported shape/count signature");
                auto& base=const_cast<DrinkProfile&>(*found);
                std::string sourceName;unsigned sourceVertices=0,sourceIndices=0;
                if(rule.contains("source_shape")){
                    const auto& source=rule.at("source_shape");Keys(source,{"name","vertices","indices"});
                    sourceName=source.at("name").get<std::string>();
                    auto sourceCount=[&](const char* key,unsigned limit){const auto& v=source.at(key);if(!v.is_number_integer()||v.get<int64_t>()<1||v.get<int64_t>()>limit)throw std::runtime_error("Invalid source geometry count");return v.get<unsigned>();};
                    sourceVertices=sourceCount("vertices",65535);sourceIndices=sourceCount("indices",3000000);
                    if(sourceName.empty()||sourceName.size()>240||sourceName.find('\0')!=std::string::npos||sourceIndices%3)throw std::runtime_error("Invalid source shape");
                }
                if(!base.materialVariants.empty()&&(base.materialSourceShape!=sourceName||base.materialSourceVertices!=sourceVertices||base.materialSourceIndices!=sourceIndices))throw std::runtime_error("Conflicting material source for this geometry");
                base.materialSourceShape=sourceName;base.materialSourceVertices=sourceVertices;base.materialSourceIndices=sourceIndices;
                auto path=rule.at("source_texture").get<std::string>();
                if(path.empty()||path.size()>512||path.find("..")!=std::string::npos||path.find(':')!=std::string::npos||path.front()=='/'||path.front()=='\\'||path.find('\0')!=std::string::npos)throw std::runtime_error("Expected relative source texture path");
                if(base.materialVariants.size()>=64)throw std::runtime_error("Maximum 64 material variants per geometry exceeded");
                for(auto& [other,p]:base.materialVariants)if(SameTexture(path,other))throw std::runtime_error("Duplicate material selector");
                auto liquid=rule.at("liquid");Keys(liquid,{"liquid_color","opacity","liquid_level","bubble_density","shaking_bubbles","surface_foam","refraction_strength","glow_strength"});
                auto controls=base.resolvedControls;controls.update(liquid);auto values=Build(controls);
                auto variant=std::make_shared<DrinkProfile>(base);variant->variants.clear();variant->materialVariants.clear();variant->resolvedControls=controls;
                auto& dst=variant->settings;dst.fill=values.fill;dst.intensity=values.intensity;dst.shakingBubbles=values.shakingBubbles;dst.foamStrength=values.foamStrength;
                dst.base.absorption=values.base.absorption;dst.base.scatter=values.base.scatter;dst.base.emission=values.base.emission;dst.base.trace.y=values.base.trace.y;dst.base.trace.z=values.base.trace.z;
                if(rule.contains("liquid_gradient")){
                    const auto& colors=rule.at("liquid_gradient");
                    if(!colors.is_array()||colors.size()<2||colors.size()>16)throw std::runtime_error("liquid_gradient needs 2..16 bottom-to-top #RRGGBB colours");
                    for(unsigned i=0;i<colors.size();i++)dst.base.liquidGradient[i]=Color(Json{{"liquid_color",colors[i]}});
                    dst.base.gradientInfo.x=float(colors.size());
                }
                if(variant->empty){dst.fill=0;dst.base.trace.y=0;dst.intensity=0;dst.shakingBubbles=0;dst.base.emission={};}
                variant->emissive=!variant->empty&&Number(controls,"glow_strength",0,10)>0;
                Motion motion;variant->distant=Simulate(dst,motion,{},{0,0,1},0,false);variant->distant.optics.w=2;variant->distant.foam=0;variant->distant.wave.x=0;
                base.materialVariants.emplace_back(std::move(path),std::move(variant));
            }
        }catch(const std::exception& e){throw std::runtime_error(file.filename().string()+": "+e.what());}
    }
    REX::INFO("Material compatibility: {} explicit source-texture rules loaded",total);
}

DrinkMap LoadDrinks(const std::filesystem::path& root,const Settings&,const Json& tuning){
    if(!tuning.is_null())ValidateTuning(tuning);
    auto doc=Read(root);DrinkMap drinks;
    auto lower=[](std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;};
    std::unordered_map<std::string,Json> overrides;
    for(auto it=doc["meshes"].begin();it!=doc["meshes"].end();++it){Controls(it.value());if(!overrides.emplace(lower(it.key()),it.value()).second)throw std::runtime_error("Duplicate mesh filename: "+it.key());}
    std::vector<std::filesystem::path> files;
    if(std::filesystem::exists(root/"profiles"))for(const auto& f:std::filesystem::directory_iterator(root/"profiles"))if(f.is_regular_file()&&lower(f.path().extension().string())==".json")files.push_back(f.path());
    std::sort(files.begin(),files.end());
    std::unordered_map<std::string,bool> known;
    unsigned count=0;
    for(const auto& file:files){try{
        std::ifstream input(file);auto pack=Json::parse(input);Keys(pack,{"schema_version","profiles"});
        if(pack.at("schema_version")!=1||!pack.at("profiles").is_array())throw std::runtime_error("Expected geometry schema 1 and profiles array");
        for(const auto& j:pack["profiles"]){
            if(++count>512)throw std::runtime_error("Maximum 512 geometry variants exceeded");
            Keys(j,{"mesh","shape","vertices","indices","empty","matrix","rings","controls","surface_detail"});
            auto mesh=j.at("mesh").get<std::string>(),shape=j.at("shape").get<std::string>();
            if(mesh.empty()||mesh.size()>240||mesh.find_first_of("/\\")!=std::string::npos||!lower(mesh).ends_with(".nif"))throw std::runtime_error("mesh must be a NIF filename");
            if(!shape.starts_with("DynamicBottleLiquids:")||shape.size()>240||shape=="DynamicBottleLiquids:")throw std::runtime_error("Expected namespaced shape tag");
            auto integer=[&](const char* key,int max){if(!j.at(key).is_number_integer())throw std::runtime_error("Expected integer geometry count");auto n=j.at(key).get<int>();if(n<1||n>max)throw std::runtime_error("Geometry count out of range");return unsigned(n);};
            DrinkProfile d;d.meshName=mesh;d.vertices=integer("vertices",65535);d.indices=integer("indices",3000000);if(d.indices%3)throw std::runtime_error("indices must contain complete triangles");d.empty=j.at("empty").get<bool>();
            MeshIdentity identity;
            auto matrix=j.at("matrix").get<std::vector<float>>();if(matrix.size()!=16)throw std::runtime_error("matrix requires 16 values");
            for(float x:matrix)if(!std::isfinite(x)||std::abs(x)>100000)throw std::runtime_error("Invalid matrix value");
            // The shader supports uniform scale and rotation, not shear/nonuniform scale.
            Vec axes[3]{{matrix[0],matrix[1],matrix[2]},{matrix[4],matrix[5],matrix[6]},{matrix[8],matrix[9],matrix[10]}};
            float scale=Length(axes[0]);if(scale<.0001f||scale>1000||matrix[12]!=0||matrix[13]!=0||matrix[14]!=0||matrix[15]!=1)throw std::runtime_error("Invalid affine matrix");
            for(int k=0;k<3;k++){if(std::abs(Length(axes[k])-scale)>scale*.001f)throw std::runtime_error("Nonuniform geometry scale unsupported");for(int n=0;n<k;n++)if(std::abs(axes[k].x*axes[n].x+axes[k].y*axes[n].y+axes[k].z*axes[n].z)>scale*scale*.001f)throw std::runtime_error("Sheared geometry matrix unsupported");}
            std::copy_n(matrix.data(),16,identity.matrix);
            const auto& rings=j.at("rings");if(!rings.is_array()||rings.size()<2||rings.size()>16)throw std::runtime_error("Expected 2 to 16 cavity rings");identity.ringCount=unsigned(rings.size());
            for(unsigned k=0;k<rings.size();k++){auto r=rings[k].get<std::vector<float>>();if(r.size()!=2||!std::isfinite(r[0])||!std::isfinite(r[1])||std::abs(r[0])>10000||r[1]<=0||r[1]>10000||(k&&r[0]<=identity.rings[k-1][0]))throw std::runtime_error("Invalid or unordered cavity rings");identity.rings[k][0]=r[0];identity.rings[k][1]=r[1];}
            auto controls=doc.at("defaults");if(j.contains("controls")){Controls(j["controls"]);controls.update(j["controls"]);}auto key=lower(mesh);known[key]=true;if(overrides.contains(key))controls.update(overrides.at(key));
            controls["reflection_strength"]=1.f; // Shared glass defaults supersede legacy pack gains.
            if(!tuning.is_null()){
                auto group=TuningGroup(tuning,mesh);if(!group.empty()&&tuning["groups"].contains(group))controls.update(tuning["groups"][group]);
                for(auto it=tuning["meshes"].begin();it!=tuning["meshes"].end();++it)if(lower(it.key())==key)controls.update(it.value());
            }
            d.resolvedControls=controls;d.settings=Build(controls,&identity);
            int detail=0;
            if(j.contains("surface_detail")){
                if(!j["surface_detail"].is_number_integer())throw std::runtime_error("surface_detail requires integer 0..2");
                detail=j["surface_detail"].get<int>();if(detail<0||detail>2)throw std::runtime_error("surface_detail requires integer 0..2");
            }
            d.settings.base.materialFlags.w=float(detail);
            if(d.empty){d.settings.fill=0;d.settings.base.trace.y=0;d.settings.intensity=0;d.settings.shakingBubbles=0;d.settings.base.emission={};}
            d.emissive=!d.empty&&Number(controls,"glow_strength",0,10)>0;
            Motion motion;d.distant=Simulate(d.settings,motion,{},{0,0,1},0,false);d.distant.optics.w=2;d.distant.foam=0;d.distant.wave.x=0;
            if(FindProfile(drinks,shape,d.vertices,d.indices))throw std::runtime_error("Ambiguous duplicate shape/count signature: "+shape);
            auto it=drinks.find(shape);if(it==drinks.end())drinks.emplace(shape,std::move(d));else it->second.variants.push_back(std::move(d));
        }
    }catch(const std::exception& e){throw std::runtime_error(file.filename().string()+": "+e.what());}}
    for(const auto& [mesh,value]:overrides)if(!known.contains(mesh))throw std::runtime_error("No geometry profile for drinks.json entry: "+mesh);
    if(drinks.empty())throw std::runtime_error("No geometry profiles found in profiles/*.json");
    OriginalSurfaces(root,drinks);LoadMaterialRules(root,drinks);return drinks;
}
}

// Configuration store of Slic3r.
//
// The configuration store is either static or dynamic.
// DynamicPrintConfig is used mainly at the user interface. while the StaticPrintConfig is used
// during the slicing and the g-code generation.
//
// The classes derived from StaticPrintConfig form a following hierarchy.
//
// FullPrintConfig
//    PrintObjectConfig
//    PrintRegionConfig
//    PrintConfig
//        GCodeConfig
//

#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include "libslic3r.h"
#include "Config.hpp"
#include "Polygon.hpp"
#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>

// #define HAS_PRESSURE_EQUALIZER

namespace Slic3r {

enum GCodeFlavor : unsigned char {
    gcfMarlinLegacy, gcfKlipper, gcfRepRapSprinter, gcfRepRapFirmware, gcfRepetier, gcfTeacup, gcfMakerWare, gcfMarlinFirmware, gcfSailfish, gcfMach3, gcfMachinekit,
    gcfSmoothie, gcfNoExtrusion
};

enum class FuzzySkinType {
    None,
    External,
    All,
    AllWalls,
};

enum PrintHostType {
    htPrusaLink, htOctoPrint, htDuet, htFlashAir, htAstroBox, htRepetier, htMKS
};

enum AuthorizationType {
    atKeyPassword, atUserPassword
};

enum InfillPattern : int {
    ipConcentric, ipRectilinear, ipGrid, ipLine, ipCubic, ipTriangles, ipStars, ipGyroid, ipHoneycomb, ipAdaptiveCubic, ipMonotonic, ipMonotonicLine, ipAlignedRectilinear, ip3DHoneycomb,
    ipHilbertCurve, ipArchimedeanChords, ipOctagramSpiral, ipSupportCubic, ipSupportBase, ipConcentricInternal,
    ipLightning, ipCrossHatch, ipZigZag, ipCrossZag,ipFloatingConcentric, ipLockedZag,
    ipCount,
};

enum EnsureVerticalThicknessLevel{
    evtDisabled,
    evtPartial,
    evtEnabled
};

enum class IroningType {
    NoIroning,
    TopSurfaces,
    TopmostOnly,
    AllSolid,
    Count,
};

//BBS
enum class WallInfillOrder {
    InnerOuterInfill,
    OuterInnerInfill,
    InfillInnerOuter,
    InfillOuterInner,
    InnerOuterInnerInfill,
    Count,
};

enum class BedTempFormula {
    btfFirstFilament,
    btfHighestTemp,
    count,
};

// BBS
enum class WallSequence {
    InnerOuter,
    OuterInner,
    InnerOuterInner,
    Count,
};
//BBS
enum class PrintSequence {
    ByLayer,
    ByObject,
    ByDefault,
    Count,
};

enum class SlicingMode
{
    // Regular, applying ClipperLib::pftNonZero rule when creating ExPolygons.
    Regular,
    // Compatible with 3DLabPrint models, applying ClipperLib::pftEvenOdd rule when creating ExPolygons.
    EvenOdd,
    // Orienting all contours CCW, thus closing all holes.
    CloseHoles,
};

enum SupportMaterialPattern {
    smpDefault,
    smpRectilinear, smpRectilinearGrid, smpHoneycomb,
    smpLightning,
    smpNone,
};

enum SupportMaterialStyle {
    smsDefault, smsGrid, smsSnug, smsTreeSlim, smsTreeStrong, smsTreeHybrid, smsTreeOrganic
};

enum LongRectrationLevel
{
    Disabled=0,
    EnableMachine,
    EnableFilament
};

enum SupportMaterialInterfacePattern {
    smipAuto, smipRectilinear, smipConcentric, smipRectilinearInterlaced, smipGrid
};

// BBS
enum SupportType {
    stNormalAuto, stTreeAuto, stNormal, stTree
};
inline bool is_tree(SupportType stype)
{
    return std::set<SupportType>{stTreeAuto, stTree}.count(stype) != 0;
};
inline bool is_tree_slim(SupportType type, SupportMaterialStyle style)
{
    return is_tree(type) && style==smsTreeSlim;
};
inline bool is_auto(SupportType stype)
{
    return std::set<SupportType>{stNormalAuto, stTreeAuto}.count(stype) != 0;
};

enum SeamPosition {
    spNearest, spAligned, spRear, spRandom
};

// Orca
enum class SeamScarfType {
    None = 0,
    External,
    All,
};

enum SLAMaterial {
    slamTough,
    slamFlex,
    slamCasting,
    slamDental,
    slamHeatResistant,
};

enum SLADisplayOrientation {
    sladoLandscape,
    sladoPortrait
};

enum SLAPillarConnectionMode {
    slapcmZigZag,
    slapcmCross,
    slapcmDynamic
};

enum BrimType {
    btAutoBrim,  // BBS
    btBrimEars,  // BBS
    btOuterOnly,
    btInnerOnly,
    btOuterAndInner,
    btNoBrim,
};

enum TimelapseType : int {
    tlTraditional = 0,
    tlSmooth
};

enum DraftShield {
    dsDisabled, dsLimited, dsEnabled
};

enum class PerimeterGeneratorType
{
    // Classic perimeter generator using Clipper offsets with constant extrusion width.
    Classic,
    // Perimeter generator with variable extrusion width based on the paper
    // "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling" ported from Cura.
    Arachne
};

enum class TopOneWallType
{
    None,
    Alltop,
    Topmost
};

// BBS
enum OverhangFanThreshold {
    Overhang_threshold_none = 0,
    Overhang_threshold_1_4,
    Overhang_threshold_2_4,
    Overhang_threshold_3_4,
    Overhang_threshold_4_4,
    Overhang_threshold_bridge
};

enum OverhangThresholdParticipatingCooling {
    Overhang_threshold_participating_cooling_none = 0,
    Overhang_threshold_participating_cooling_1_4,
    Overhang_threshold_participating_cooling_2_4,
    Overhang_threshold_participating_cooling_3_4,
    Overhang_threshold_participating_cooling_4_4,
    Overhang_threshold_participating_cooling_bridge
};

// BBS
enum BedType {
    btDefault = 0,
    btPC,
    btEP,
    btPEI,
    btPTE,
    btSuperTack,
    btCount
};

enum class ExtruderOnlyAreaType:unsigned char {
    btNoArea= 0,
    Engilish,
    Chinese,
    btAreaCount
};

// BBS
enum LayerSeq {
    flsAuto,
    flsCutomize
};

// BBS
enum NozzleType {
    ntUndefine = 0,
    ntHardenedSteel,
    ntStainlessSteel,
    ntTungstenCarbide,
    ntBrass,
    ntCount
};

static std::unordered_map<NozzleType, std::string>NozzleTypeEumnToStr = {
    {NozzleType::ntUndefine,        "undefine"},
    {NozzleType::ntHardenedSteel,   "hardened_steel"},
    {NozzleType::ntStainlessSteel,  "stainless_steel"},
    {NozzleType::ntTungstenCarbide, "tungsten_carbide"},
    {NozzleType::ntBrass,           "brass"}
};

static std::unordered_map<std::string, NozzleType>NozzleTypeStrToEumn = {
    {"undefine", NozzleType::ntUndefine},
    {"hardened_steel", NozzleType::ntHardenedSteel},
    {"stainless_steel", NozzleType::ntStainlessSteel},
    {"tungsten_carbide", NozzleType::ntTungstenCarbide},
    {"brass", NozzleType::ntBrass}
};

// BBS
enum PrinterStructure {
    psUndefine=0,
    psCoreXY,
    psI3,
    psHbot,
    psDelta
};

// BBS
enum ZHopType {
    zhtAuto = 0,
    zhtNormal,
    zhtSlope,
    zhtSpiral,
    zhtCount
};

// BBS
enum ExtruderType {
    etDirectDrive = 0,
    etBowden,
    etMaxExtruderType = etBowden
};

enum NozzleVolumeType {
    nvtStandard = 0,
    nvtHighFlow,
    nvtMaxNozzleVolumeType = nvtHighFlow
};

enum FilamentMapMode {
    fmmAutoForFlush,
    fmmAutoForMatch,
    fmmManual,
    fmmDefault
};

extern std::string get_extruder_variant_string(ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type);

std::string get_nozzle_volume_type_string(NozzleVolumeType nozzle_volume_type);

static std::string bed_type_to_gcode_string(const BedType type)
{
    std::string type_str;

    switch (type) {
    case btSuperTack:
        type_str = "supertack_plate";
        break;
    case btPC:
        type_str = "cool_plate";
        break;
    case btEP:
        type_str = "eng_plate";
        break;
    case btPEI:
        type_str = "hot_plate";
        break;
    case btPTE:
        type_str = "textured_plate";
        break;
    default:
        type_str = "unknown";
        break;
    }

    return type_str;
}

static std::string get_bed_temp_key(const BedType type)
{
    if (type == btSuperTack)
        return "supertack_plate_temp";

    if (type == btPC)
        return "cool_plate_temp";

    if (type == btEP)
        return "eng_plate_temp";

    if (type == btPEI)
        return "hot_plate_temp";

    if (type == btPTE)
        return "textured_plate_temp";

    return "";
}

static std::string get_bed_temp_1st_layer_key(const BedType type)
{
    if (type == btSuperTack)
        return "supertack_plate_temp_initial_layer";

    if (type == btPC)
        return "cool_plate_temp_initial_layer";

    if (type == btEP)
        return "eng_plate_temp_initial_layer";

    if (type == btPEI)
        return "hot_plate_temp_initial_layer";

    if (type == btPTE)
        return "textured_plate_temp_initial_layer";

    return "";
}

extern const std::vector<std::string> filament_extruder_override_keys;

// for parse extruder_ams_count
extern std::vector<std::map<int, int>> get_extruder_ams_count(const std::vector<std::string> &strs);
extern std::vector<std::string> save_extruder_ams_count_to_string(const std::vector<std::map<int, int>> &extruder_ams_count);

#define CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(NAME) \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names(); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values();

CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrinterTechnology)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(GCodeFlavor)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(FuzzySkinType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(InfillPattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(IroningType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SlicingMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialPattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialStyle)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialInterfacePattern)
// BBS
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SeamPosition)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SeamScarfType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLADisplayOrientation)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLAPillarConnectionMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(BrimType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(TimelapseType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(BedType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(DraftShield)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)

CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrintHostType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(AuthorizationType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PerimeterGeneratorType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(TopOneWallType)
#undef CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS

// Defines each and every confiuration option of Slic3r, including the properties of the GUI dialogs.
// Does not store the actual values, but defines default values.
class PrintConfigDef : public ConfigDef
{
public:
    PrintConfigDef();

    static void handle_legacy(t_config_option_key &opt_key, std::string &value);

    // Array options growing with the number of extruders
    const std::vector<std::string>& extruder_option_keys() const { return m_extruder_option_keys; }
    // Options defining the extruder retract properties. These keys are sorted lexicographically.
    // The extruder retract keys could be overidden by the same values defined at the Filament level
    // (then the key is further prefixed with the "filament_" prefix).
    const std::vector<std::string>& extruder_retract_keys() const { return m_extruder_retract_keys; }

    // BBS
    const std::vector<std::string>& filament_option_keys() const { return m_filament_option_keys; }
    const std::vector<std::string>& filament_retract_keys() const { return m_filament_retract_keys; }

private:
    void init_common_params();
    void init_fff_params();
    void init_extruder_option_keys();
    void init_sla_params();

    std::vector<std::string>    m_extruder_option_keys;
    std::vector<std::string>    m_extruder_retract_keys;

    // BBS
    void init_filament_option_keys();

    std::vector<std::string>    m_filament_option_keys;
    std::vector<std::string>    m_filament_retract_keys;
};

// The one and only global definition of SLic3r configuration options.
// This definition is constant.
extern const PrintConfigDef print_config_def;

class StaticPrintConfig;

// Minimum object distance for arrangement, based on printer technology.
double min_object_distance(const ConfigBase &cfg);

// Slic3r dynamic configuration, used to override the configuration
// per object, per modification volume or per printing material.
// The dynamic configuration is also used to store user modifications of the print global parameters,
// so the modified configuration values may be diffed against the active configuration
// to invalidate the proper slicing resp. g-code generation processing steps.
// This object is mapped to Perl as Slic3r::Config.
class DynamicPrintConfig : public DynamicConfig
{
public:
    DynamicPrintConfig() {}
    DynamicPrintConfig(const DynamicPrintConfig &rhs) : DynamicConfig(rhs) {}
    DynamicPrintConfig(DynamicPrintConfig &&rhs) noexcept : DynamicConfig(std::move(rhs)) {}
    explicit DynamicPrintConfig(const StaticPrintConfig &rhs);
    explicit DynamicPrintConfig(const ConfigBase &rhs) : DynamicConfig(rhs) {}

    DynamicPrintConfig& operator=(const DynamicPrintConfig &rhs) { DynamicConfig::operator=(rhs); return *this; }
    DynamicPrintConfig& operator=(DynamicPrintConfig &&rhs) noexcept { DynamicConfig::operator=(std::move(rhs)); return *this; }

    static DynamicPrintConfig  full_print_config();
    static DynamicPrintConfig* new_from_defaults_keys(const std::vector<std::string> &keys);

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }

    void                normalize_fdm(int used_filaments = 0);
    void                normalize_fdm_1();
    //return the changed param set
    t_config_option_keys normalize_fdm_2(int num_objects, int used_filaments = 0);

    size_t              get_parameter_size(const std::string& param_name, size_t extruder_nums);
    void                set_num_extruders(unsigned int num_extruders);

    // BBS
    void                set_num_filaments(unsigned int num_filaments);

    //BBS
    // Validate the PrintConfig. Returns an empty string on success, otherwise an error message is returned.
    std::map<std::string, std::string>         validate(bool under_cli = false);

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    //BBS special case Support G/ Support W
    std::string get_filament_type(std::string &displayed_filament_type, int id = 0);

    //BBS
    bool is_using_different_extruders();
    bool support_different_extruders(int& extruder_count);
    int get_index_for_extruder(int extruder_or_filament_id, std::string id_name, ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type, std::string variant_name, unsigned int stride = 1) const;
    std::vector<int> update_values_to_printer_extruders(DynamicPrintConfig& printer_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name, unsigned int stride = 1, unsigned int extruder_id = 0);
    void update_values_to_printer_extruders_for_multiple_filaments(DynamicPrintConfig& printer_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name);

    void update_non_diff_values_to_base_config(DynamicPrintConfig& new_config, const t_config_option_keys& keys, const std::set<std::string>& different_keys, std::string extruder_id_name, std::string extruder_variant_name,
        std::set<std::string>& key_set1, std::set<std::string>& key_set2);
    void update_diff_values_to_child_config(DynamicPrintConfig& new_config, std::string extruder_id_name, std::string extruder_variant_name, std::set<std::string>& key_set1, std::set<std::string>& key_set2);

    int update_values_from_single_to_multi(DynamicPrintConfig& multi_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name);
    int update_values_from_multi_to_single(DynamicPrintConfig& single_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name, std::vector<std::string>& extruder_variants);

    int update_values_from_single_to_multi_2(DynamicPrintConfig& multi_config, std::set<std::string>& key_set);
    int update_values_from_multi_to_single_2(std::set<std::string>& key_set);

public:
    // query filament
    std::string get_filament_vendor() const;
    std::string get_filament_type() const;
};
extern std::set<std::string> printer_extruder_options;
extern std::set<std::string> print_options_with_variant;
extern std::set<std::string> filament_options_with_variant;
extern std::set<std::string> printer_options_with_variant_1;
extern std::set<std::string> printer_options_with_variant_2;
extern std::set<std::string> empty_options;

extern void update_static_print_config_from_dynamic(ConfigBase& config, const DynamicPrintConfig& dest_config, std::vector<int> variant_index, std::set<std::string>& key_set1, int stride = 1);
extern void compute_filament_override_value(const std::string& opt_key, const ConfigOption *opt_old_machine, const ConfigOption *opt_new_machine, const ConfigOption *opt_new_filament, const DynamicPrintConfig& new_full_config,
    t_config_option_keys& diff_keys, DynamicPrintConfig& filament_overrides, std::vector<int>& f_maps);

void handle_legacy_sla(DynamicPrintConfig &config);

class StaticPrintConfig : public StaticConfig
{
public:
    StaticPrintConfig() {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }
    // Reference to the cached list of keys.
    virtual const t_config_option_keys& keys_ref() const = 0;

protected:
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    // Internal class for keeping a dynamic map to static options.
    class StaticCacheBase
    {
    public:
        // To be called during the StaticCache setup.
        // Add one ConfigOption into m_map_name_to_offset.
        template<typename T>
        void                opt_add(const std::string &name, const char *base_ptr, const T &opt)
        {
            assert(m_map_name_to_offset.find(name) == m_map_name_to_offset.end());
            m_map_name_to_offset[name] = (const char*)&opt - base_ptr;
        }

    protected:
        std::map<std::string, ptrdiff_t>    m_map_name_to_offset;
    };

    // Parametrized by the type of the topmost class owning the options.
    template<typename T>
    class StaticCache : public StaticCacheBase
    {
    public:
        // Calling the constructor of m_defaults with 0 forces m_defaults to not run the initialization.
        StaticCache() : m_defaults(nullptr) {}
        ~StaticCache() { delete m_defaults; m_defaults = nullptr; }

        bool                initialized() const { return ! m_keys.empty(); }

        ConfigOption*       optptr(const std::string &name, T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<ConfigOption*>((char*)owner + it->second);
        }

        const ConfigOption* optptr(const std::string &name, const T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<const ConfigOption*>((const char*)owner + it->second);
        }

        const std::vector<std::string>& keys()      const { return m_keys; }
        const T&                        defaults()  const { return *m_defaults; }

        // To be called during the StaticCache setup.
        // Collect option keys from m_map_name_to_offset,
        // assign default values to m_defaults.
        void                finalize(T *defaults, const ConfigDef *defs)
        {
            assert(defs != nullptr);
            m_defaults = defaults;
            m_keys.clear();
            m_keys.reserve(m_map_name_to_offset.size());
            for (const auto &kvp : defs->options) {
                // Find the option given the option name kvp.first by an offset from (char*)m_defaults.
                ConfigOption *opt = this->optptr(kvp.first, m_defaults);
                if (opt == nullptr)
                    // This option is not defined by the ConfigBase of type T.
                    continue;
                m_keys.emplace_back(kvp.first);
                const ConfigOptionDef *def = defs->get(kvp.first);
                assert(def != nullptr);
                if (def->default_value)
                    opt->set(def->default_value.get());
            }
        }

    private:
        T                                  *m_defaults;
        std::vector<std::string>            m_keys;
    };
};

#define STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    const ConfigOption*      optptr(const t_config_option_key &opt_key) const override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    ConfigOption*            optptr(const t_config_option_key &opt_key, bool create = false) override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::keys(). Collect names of all configuration values maintained by this configuration store. */ \
    t_config_option_keys     keys() const override { return s_cache_##CLASS_NAME.keys(); } \
    const t_config_option_keys& keys_ref() const override { return s_cache_##CLASS_NAME.keys(); } \
    static const CLASS_NAME& defaults() { assert(s_cache_##CLASS_NAME.initialized()); return s_cache_##CLASS_NAME.defaults(); } \
private: \
    friend int print_config_static_initializer(); \
    static void initialize_cache() \
    { \
        assert(! s_cache_##CLASS_NAME.initialized()); \
        if (! s_cache_##CLASS_NAME.initialized()) { \
            CLASS_NAME *inst = new CLASS_NAME(1); \
            inst->initialize(s_cache_##CLASS_NAME, (const char*)inst); \
            s_cache_##CLASS_NAME.finalize(inst, inst->def()); \
        } \
    } \
    /* Cache object holding a key/option map, a list of option keys and a copy of this static config initialized with the defaults. */ \
    static StaticPrintConfig::StaticCache<CLASS_NAME> s_cache_##CLASS_NAME;

#define STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Public default constructor will initialize the key/option cache and the default object copy if needed. */ \
    CLASS_NAME() { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
protected: \
    /* Protected constructor to be called when compounded. */ \
    CLASS_NAME(int) {}

#define STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here. */ \
    const ConfigDef*    def() const override { return &print_config_def; } \
    /* Handle legacy and obsoleted config keys */ \
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override \
        { PrintConfigDef::handle_legacy(opt_key, value); }

#define PRINT_CONFIG_CLASS_ELEMENT_DEFINITION(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(KEY) cache.opt_add(BOOST_PP_STRINGIZE(KEY), base_ptr, this->KEY);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION(r, data, elem) PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(BOOST_PP_TUPLE_ELEM(1, elem))
#define PRINT_CONFIG_CLASS_ELEMENT_HASH(r, data, elem) boost::hash_combine(seed, BOOST_PP_TUPLE_ELEM(1, elem).hash());
#define PRINT_CONFIG_CLASS_ELEMENT_EQUAL(r, data, elem) if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;
#define PRINT_CONFIG_CLASS_ELEMENT_LOWER(r, data, elem) \
        if (BOOST_PP_TUPLE_ELEM(1, elem) < rhs.BOOST_PP_TUPLE_ELEM(1, elem)) return true; \
        if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;

#define PRINT_CONFIG_CLASS_DEFINE(CLASS_NAME, PARAMETER_DEFINITION_SEQ) \
class CLASS_NAME : public StaticPrintConfig { \
    STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
public: \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ) \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ) \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ) \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
    bool operator<(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_LOWER, _, PARAMETER_DEFINITION_SEQ) \
        return false; \
    } \
protected: \
    void initialize(StaticCacheBase &cache, const char *base_ptr) \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ) \
    } \
};

#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM(r, data, i, elem) BOOST_PP_COMMA_IF(i) public elem
#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM(r, VALUE, i, elem) BOOST_PP_COMMA_IF(i) elem(VALUE)
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, VALUE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM, VALUE, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM(r, data, elem) this->elem::initialize(cache, base_ptr);
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_HASH(r, data, elem) boost::hash_combine(seed, static_cast<const elem*>(this)->hash());
#define PRINT_CONFIG_CLASS_DERIVED_EQUAL(r, data, elem) \
    if (! (*static_cast<const elem*>(this) == static_cast<const elem&>(rhs))) return false;

// Generic version, with or without new parameters. Don't use this directly.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION, PARAMETER_REGISTRATION, PARAMETER_HASHES, PARAMETER_EQUALS) \
class CLASS_NAME : PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) { \
    STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    CLASS_NAME() : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 0) { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
public: \
    PARAMETER_DEFINITION \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_HASH, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_HASHES \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_EQUAL, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_EQUALS \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
protected: \
    CLASS_NAME(int) : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 1) {} \
    void initialize(StaticCacheBase &cache, const char* base_ptr) { \
        PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) \
        PARAMETER_REGISTRATION \
    } \
};
// Variant without adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE0(CLASS_NAME, CLASSES_PARENTS_TUPLE) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY())
// Variant with adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION_SEQ) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ))

// This object is mapped to Perl as Slic3r::Config::PrintObject.
PRINT_CONFIG_CLASS_DEFINE(
    PrintObjectConfig,

    ((ConfigOptionFloat,               brim_object_gap))
    ((ConfigOptionEnum<BrimType>,      brim_type))
    ((ConfigOptionFloat,               brim_width))
    ((ConfigOptionBool,                bridge_no_support))
    ((ConfigOptionFloat,               elefant_foot_compensation))
    ((ConfigOptionFloat,               max_bridge_length))
    ((ConfigOptionFloat,               line_width))
    // Force the generation of solid shells between adjacent materials/volumes.
    ((ConfigOptionBool,                interface_shells))
    ((ConfigOptionFloat,               layer_height))
    ((ConfigOptionFloat,               mmu_segmented_region_max_width))
    ((ConfigOptionFloat,               mmu_segmented_region_interlocking_depth))
    ((ConfigOptionFloat,               raft_contact_distance))
    ((ConfigOptionFloat,               raft_expansion))
    ((ConfigOptionPercent,             raft_first_layer_density))
    ((ConfigOptionFloat,               raft_first_layer_expansion))
    ((ConfigOptionInt,                 raft_layers))
    ((ConfigOptionEnum<SeamPosition>,  seam_position))
    ((ConfigOptionBool,                seam_placement_away_from_overhangs))
    ((ConfigOptionFloat,               slice_closing_radius))
    ((ConfigOptionEnum<SlicingMode>,   slicing_mode))
    ((ConfigOptionBool,                enable_support))
    // Automatic supports (generated based on support_threshold_angle).
    ((ConfigOptionEnum<SupportType>,   support_type))
    // Direction of the support pattern (in XY plane).`
    ((ConfigOptionFloat,               support_angle))
    ((ConfigOptionBool,                support_on_build_plate_only))
    ((ConfigOptionBool,                support_critical_regions_only))
    ((ConfigOptionBool,                support_remove_small_overhang))
    ((ConfigOptionFloat,               support_top_z_distance))
    ((ConfigOptionFloat,               support_bottom_z_distance))
    ((ConfigOptionInt,                 enforce_support_layers))
    ((ConfigOptionInt,                 support_filament))
    ((ConfigOptionFloat,               support_line_width))
    ((ConfigOptionBool,                support_interface_not_for_body))
    ((ConfigOptionBool,                support_interface_loop_pattern))
    ((ConfigOptionInt,                 support_interface_filament))
    ((ConfigOptionInt,                 support_interface_top_layers))
    ((ConfigOptionInt,                 support_interface_bottom_layers))
    // Spacing between interface lines (the hatching distance). Set zero to get a solid interface.
    ((ConfigOptionFloat,               support_interface_spacing))
    ((ConfigOptionFloatsNullable,      support_interface_speed))
    ((ConfigOptionEnum<SupportMaterialPattern>, support_base_pattern))
    ((ConfigOptionEnum<SupportMaterialInterfacePattern>, support_interface_pattern))
    // Spacing between support material lines (the hatching distance).
    ((ConfigOptionFloat,               support_base_pattern_spacing))
    ((ConfigOptionFloat,               support_expansion))
    ((ConfigOptionFloatsNullable,      support_speed))
    ((ConfigOptionEnum<SupportMaterialStyle>, support_style))
    // BBS
    //((ConfigOptionBool,                independent_support_layer_height))
    ((ConfigOptionBool,                thick_bridges))
    // Overhang angle threshold.
    ((ConfigOptionInt,                 support_threshold_angle))
    ((ConfigOptionFloat,               support_object_xy_distance))
    ((ConfigOptionFloat,               support_object_first_layer_gap))
    ((ConfigOptionFloat,               xy_hole_compensation))
    ((ConfigOptionFloat,               xy_contour_compensation))
    //BBS auto hole contour compensation
    ((ConfigOptionBool,               enable_circle_compensation))
    ((ConfigOptionFloat,              circle_compensation_manual_offset))
    ((ConfigOptionBool,               apply_scarf_seam_on_circles))
    ((ConfigOptionBool,                flush_into_objects))
    // BBS
    ((ConfigOptionBool,                flush_into_infill))
    ((ConfigOptionBool,                flush_into_support))
    ((ConfigOptionEnum<WallSequence>,  wall_sequence))
    // BBS
    ((ConfigOptionFloat,              tree_support_branch_distance))
    ((ConfigOptionFloat,              tree_support_branch_diameter))
    ((ConfigOptionFloat,              tree_support_branch_angle))
    ((ConfigOptionFloat,              tree_support_branch_diameter_angle))
    ((ConfigOptionInt,                tree_support_wall_count))
    ((ConfigOptionBool,               detect_narrow_internal_solid_infill))
    ((ConfigOptionBool,               detect_floating_vertical_shell))
    // ((ConfigOptionBool,               adaptive_layer_height))
    ((ConfigOptionFloat,              support_bottom_interface_spacing))
    ((ConfigOptionFloat,              internal_bridge_support_thickness))
    ((ConfigOptionEnum<PerimeterGeneratorType>, wall_generator))
    ((ConfigOptionPercent,            wall_transition_length))
    ((ConfigOptionPercent,            wall_transition_filter_deviation))
    ((ConfigOptionFloat,              wall_transition_angle))
    ((ConfigOptionInt,                wall_distribution_count))
    ((ConfigOptionPercent,            min_feature_size))
    ((ConfigOptionPercent,            min_bead_width))
    ((ConfigOptionEnum<TopOneWallType>, top_one_wall_type))
    ((ConfigOptionPercent,            top_area_threshold))
    ((ConfigOptionBool,               only_one_wall_first_layer))
    // OrcaSlicer
    ((ConfigOptionPercent,            seam_gap))
    ((ConfigOptionPercent,            wipe_speed))
    ((ConfigOptionBool,               role_base_wipe_speed))
    ((ConfigOptionBool,               precise_z_height)) // BBS

    ((ConfigOptionBool, interlocking_beam))
    ((ConfigOptionFloat,interlocking_beam_width))
    ((ConfigOptionFloat,interlocking_orientation))
    ((ConfigOptionInt,  interlocking_beam_layer_count))
    ((ConfigOptionInt,  interlocking_depth))
    ((ConfigOptionInt,  interlocking_boundary_avoidance))
    ((ConfigOptionInt,  scarf_angle_threshold))

)

// This object is mapped to Perl as Slic3r::Config::PrintRegion.
PRINT_CONFIG_CLASS_DEFINE(
    PrintRegionConfig,

    ((ConfigOptionInts,  print_extruder_id))
    ((ConfigOptionStrings,  print_extruder_variant))
    ((ConfigOptionInt, bottom_shell_layers))
    ((ConfigOptionFloat, bottom_shell_thickness))
    ((ConfigOptionFloat, bridge_angle))
    ((ConfigOptionFloat, bridge_flow))
    ((ConfigOptionFloatsNullable, overhang_totally_speed))
    ((ConfigOptionFloatsNullable, bridge_speed))
    ((ConfigOptionEnum<EnsureVerticalThicknessLevel>, ensure_vertical_shell_thickness))
    ((ConfigOptionEnum<InfillPattern>, top_surface_pattern))
    ((ConfigOptionEnum<InfillPattern>, bottom_surface_pattern))
    ((ConfigOptionEnum<InfillPattern>, internal_solid_infill_pattern))
    ((ConfigOptionFloat, outer_wall_line_width))
    ((ConfigOptionFloatsNullable, outer_wall_speed))
    ((ConfigOptionFloat, infill_direction))
    ((ConfigOptionBool, symmetric_infill_y_axis))
    ((ConfigOptionFloat, infill_shift_step))
    ((ConfigOptionFloat, infill_rotate_step))
    ((ConfigOptionPercent, skeleton_infill_density))
    ((ConfigOptionPercent, skin_infill_density))
    ((ConfigOptionPercent, sparse_infill_density))
    ((ConfigOptionFloat, infill_lock_depth))
    ((ConfigOptionFloat, skin_infill_depth))
    ((ConfigOptionEnum<InfillPattern>, sparse_infill_pattern))
    ((ConfigOptionEnum<InfillPattern>, locked_skin_infill_pattern))
    ((ConfigOptionEnum<InfillPattern>, locked_skeleton_infill_pattern))
    ((ConfigOptionEnum<FuzzySkinType>, fuzzy_skin))
    ((ConfigOptionFloat, fuzzy_skin_thickness))
    ((ConfigOptionFloat, fuzzy_skin_point_distance))
    ((ConfigOptionFloatsNullable, gap_infill_speed))
    ((ConfigOptionInt, sparse_infill_filament))
    ((ConfigOptionFloat, sparse_infill_line_width))
    ((ConfigOptionFloat, skin_infill_line_width))
    ((ConfigOptionFloat, skeleton_infill_line_width))
    ((ConfigOptionPercent, infill_wall_overlap))
    ((ConfigOptionFloatsNullable, sparse_infill_speed))
    //BBS
    ((ConfigOptionBool, infill_combination))
    // Ironing options
    ((ConfigOptionEnum<IroningType>, ironing_type))
    ((ConfigOptionEnum<InfillPattern>, ironing_pattern))
    ((ConfigOptionPercent, ironing_flow))
    ((ConfigOptionFloat, ironing_spacing))
    ((ConfigOptionFloat, ironing_inset))
    ((ConfigOptionFloat, ironing_direction))
    ((ConfigOptionFloat, ironing_speed))
    // Detect bridging perimeters
    ((ConfigOptionBool, detect_overhang_wall))
    ((ConfigOptionBool, smooth_speed_discontinuity_area))
    ((ConfigOptionFloat, smooth_coefficient))
    ((ConfigOptionInt, wall_filament))
    ((ConfigOptionFloat, inner_wall_line_width))
    ((ConfigOptionFloatsNullable, inner_wall_speed))
    // Total number of perimeters.
    ((ConfigOptionInt, wall_loops))
    ((ConfigOptionFloat, minimum_sparse_infill_area))
    ((ConfigOptionInt, solid_infill_filament))
    ((ConfigOptionFloat, internal_solid_infill_line_width))
    ((ConfigOptionFloatsNullable, internal_solid_infill_speed))
    // Detect thin walls.
    ((ConfigOptionBool, detect_thin_wall))
    ((ConfigOptionFloat, top_surface_line_width))
    ((ConfigOptionInt, top_shell_layers))
    ((ConfigOptionFloat, top_shell_thickness))
    ((ConfigOptionFloatsNullable, top_surface_speed))
    ((ConfigOptionFloatsOrPercentsNullable, small_perimeter_speed))
    ((ConfigOptionFloatsNullable, small_perimeter_threshold))
    ((ConfigOptionFloatsOrPercentsNullable, vertical_shell_speed))
    ((ConfigOptionInt, top_color_penetration_layers))
    ((ConfigOptionInt, bottom_color_penetration_layers))
    //BBS
    ((ConfigOptionBoolsNullable, enable_overhang_speed))
    ((ConfigOptionFloatsNullable, overhang_1_4_speed))
    ((ConfigOptionFloatsNullable, overhang_2_4_speed))
    ((ConfigOptionFloatsNullable, overhang_3_4_speed))
    ((ConfigOptionFloatsNullable, overhang_4_4_speed))
    ((ConfigOptionBoolsNullable, enable_height_slowdown))
    ((ConfigOptionFloatsNullable, slowdown_start_height))
    ((ConfigOptionFloatsNullable, slowdown_start_speed))
    ((ConfigOptionFloatsNullable, slowdown_start_acc))
    ((ConfigOptionFloatsNullable, slowdown_end_height))
    ((ConfigOptionFloatsNullable, slowdown_end_speed))
    ((ConfigOptionFloatsNullable, slowdown_end_acc))
    ((ConfigOptionFloatOrPercent, sparse_infill_anchor))
    ((ConfigOptionFloatOrPercent, sparse_infill_anchor_max))
    //OrcaSlicer
    ((ConfigOptionFloat, top_solid_infill_flow_ratio))
    ((ConfigOptionFloat, initial_layer_flow_ratio))
    ((ConfigOptionFloat, filter_out_gap_fill))
    ((ConfigOptionBool, precise_outer_wall))
    //calib
    ((ConfigOptionFloat, print_flow_ratio))
    // Orca: seam slopes
    ((ConfigOptionBool,                 override_filament_scarf_seam_setting))
    ((ConfigOptionEnum<SeamScarfType>,  seam_slope_type))
    ((ConfigOptionBool,                 seam_slope_conditional))
    ((ConfigOptionFloatOrPercent,       seam_slope_start_height))
    ((ConfigOptionFloatOrPercent,       seam_slope_gap))
    ((ConfigOptionBool,                 seam_slope_entire_loop))
    ((ConfigOptionFloat,                seam_slope_min_length))
    ((ConfigOptionInt,                  seam_slope_steps))
    ((ConfigOptionBool,                 seam_slope_inner_walls))
)

PRINT_CONFIG_CLASS_DEFINE(
    MachineEnvelopeConfig,

    // M201 X... Y... Z... E... [mm/sec^2]
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_x))
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_y))
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_z))
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_e))
    // M203 X... Y... Z... E... [mm/sec]
    ((ConfigOptionFloatsNullable,       machine_max_speed_x))
    ((ConfigOptionFloatsNullable,       machine_max_speed_y))
    ((ConfigOptionFloatsNullable,       machine_max_speed_z))
    ((ConfigOptionFloatsNullable,       machine_max_speed_e))

    // M204 P... R... T...[mm/sec^2]
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_extruding))
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_retracting))
    ((ConfigOptionFloatsNullable,       machine_max_acceleration_travel))

    // M205 X... Y... Z... E... [mm/sec]
    ((ConfigOptionFloatsNullable,       machine_max_jerk_x))
    ((ConfigOptionFloatsNullable,       machine_max_jerk_y))
    ((ConfigOptionFloatsNullable,       machine_max_jerk_z))
    ((ConfigOptionFloatsNullable,       machine_max_jerk_e))
    // M205 T... [mm/sec]
    ((ConfigOptionFloatsNullable,       machine_min_travel_rate))
    // M205 S... [mm/sec]
    ((ConfigOptionFloatsNullable,       machine_min_extruding_rate))
)

// This object is mapped to Perl as Slic3r::Config::GCode.
PRINT_CONFIG_CLASS_DEFINE(
    GCodeConfig,

    ((ConfigOptionString,              before_layer_change_gcode))
    ((ConfigOptionString,              printing_by_object_gcode))
    ((ConfigOptionFloatsNullable,      deretraction_speed))
    //BBS
    ((ConfigOptionBool,                enable_arc_fitting))
    ((ConfigOptionString,              machine_end_gcode))
    ((ConfigOptionStrings,             filament_end_gcode))
    ((ConfigOptionFloatsNullable,      filament_flow_ratio))
    ((ConfigOptionBools,               enable_pressure_advance))
    ((ConfigOptionFloats,              pressure_advance))
    ((ConfigOptionFloats,              filament_diameter))
    ((ConfigOptionInts,              filament_adhesiveness_category))
    ((ConfigOptionFloats,              filament_density))
    ((ConfigOptionStrings,             filament_type))
    ((ConfigOptionBools,               filament_soluble))
    ((ConfigOptionStrings,             filament_ids))
    ((ConfigOptionStrings,             filament_colour))
    ((ConfigOptionStrings,             filament_vendor))
    ((ConfigOptionBools,               filament_is_support))
    ((ConfigOptionInts,                filament_printable))
    ((ConfigOptionEnumsGeneric,        filament_scarf_seam_type))
    ((ConfigOptionFloatsOrPercents,    filament_scarf_height))
    ((ConfigOptionFloatsOrPercents,    filament_scarf_gap))
    ((ConfigOptionFloats,              filament_scarf_length))
    ((ConfigOptionFloats,              filament_change_length))
    ((ConfigOptionFloats,              filament_cost))
    ((ConfigOptionFloats,              impact_strength_z))
    ((ConfigOptionString,              filament_notes))
    ((ConfigOptionStrings,             default_filament_colour))
    ((ConfigOptionInts,                temperature_vitrification))  //BBS
    ((ConfigOptionFloatsNullable,      filament_ramming_travel_time))  //BBS
    ((ConfigOptionIntsNullable,        filament_pre_cooling_temperature))// BBS
    ((ConfigOptionFloatsNullable,      filament_max_volumetric_speed))
    ((ConfigOptionFloatsNullable,      filament_ramming_volumetric_speed))
    ((ConfigOptionFloat,               prime_tower_lift_speed))
    ((ConfigOptionFloat,               prime_tower_lift_height))
    ((ConfigOptionInts,                required_nozzle_HRC))
    ((ConfigOptionEnum<FilamentMapMode>, filament_map_mode))
    ((ConfigOptionInts,                filament_map))
    //((ConfigOptionInts,                filament_extruder_id))
    ((ConfigOptionStrings,             filament_extruder_variant))
    ((ConfigOptionFloat,               machine_load_filament_time))
    ((ConfigOptionFloat,               machine_unload_filament_time))
    ((ConfigOptionFloat,               machine_switch_extruder_time))
    ((ConfigOptionFloat,               machine_prepare_compensation_time))
    ((ConfigOptionBool,                enable_pre_heating))
    ((ConfigOptionEnum<BedTempFormula>, bed_temperature_formula))
    ((ConfigOptionInts,                physical_extruder_map))
    ((ConfigOptionFloatsNullable,      hotend_cooling_rate))
    ((ConfigOptionFloatsNullable,      hotend_heating_rate))
    ((ConfigOptionIntsNullable,        nozzle_flush_dataset))
    ((ConfigOptionFloats,              filament_minimal_purge_on_wipe_tower))
    ((ConfigOptionFloatsNullable,      filament_flush_volumetric_speed))
    ((ConfigOptionIntsNullable,        filament_flush_temp))
    // BBS
    ((ConfigOptionBool,                scan_first_layer))
    ((ConfigOptionPoints,              thumbnail_size))
    // ((ConfigOptionBool,                spaghetti_detector))
    ((ConfigOptionBool,                gcode_add_line_number))
    ((ConfigOptionBool,                bbl_bed_temperature_gcode))
    ((ConfigOptionEnum<GCodeFlavor>,   gcode_flavor))
    ((ConfigOptionString,              layer_change_gcode))
    ((ConfigOptionString,              time_lapse_gcode))
//#ifdef HAS_PRESSURE_EQUALIZER
//    ((ConfigOptionFloat,               max_volumetric_extrusion_rate_slope_positive))
//    ((ConfigOptionFloat,               max_volumetric_extrusion_rate_slope_negative))
//#endif
    ((ConfigOptionPercentsNullable,    retract_before_wipe))
    ((ConfigOptionFloatsNullable,      retraction_length))
    ((ConfigOptionFloatsNullable,      retract_length_toolchange))
    ((ConfigOptionInt,                 enable_long_retraction_when_cut))
    ((ConfigOptionFloatsNullable,      retraction_distances_when_cut))
    ((ConfigOptionBoolsNullable,       long_retractions_when_cut))
    ((ConfigOptionFloatsNullable,      retraction_distances_when_ec))
    ((ConfigOptionBoolsNullable,       long_retractions_when_ec))
    ((ConfigOptionFloatsNullable,      z_hop))
    // BBS
    ((ConfigOptionEnumsGenericNullable,z_hop_types))
    ((ConfigOptionFloatsNullable,      retract_restart_extra))
    ((ConfigOptionFloatsNullable,      retract_restart_extra_toolchange))
    ((ConfigOptionFloatsNullable,      retraction_speed))
    ((ConfigOptionFloatsNullable,      retract_lift_above))
    ((ConfigOptionFloatsNullable,      retract_lift_below))
    ((ConfigOptionString,              machine_start_gcode))
    ((ConfigOptionStrings,             filament_start_gcode))
    ((ConfigOptionBool,                single_extruder_multi_material))
    ((ConfigOptionBool,                wipe_tower_no_sparse_layers))
    ((ConfigOptionString,              change_filament_gcode))
    ((ConfigOptionFloatsNullable,      travel_speed))
    ((ConfigOptionFloatsNullable,      travel_speed_z))
    ((ConfigOptionBool,                use_relative_e_distances))
    ((ConfigOptionBool,                use_firmware_retraction))
    ((ConfigOptionBool,                silent_mode))
    ((ConfigOptionString,              machine_pause_gcode))
    ((ConfigOptionString,              template_custom_gcode))
    //BBS
    ((ConfigOptionEnumsGenericNullable,nozzle_type))
    ((ConfigOptionEnum<PrinterStructure>,printer_structure))
    ((ConfigOptionBool,                auxiliary_fan))
    ((ConfigOptionBool,                support_chamber_temp_control))
    ((ConfigOptionBool,                apply_top_surface_compensation))
    ((ConfigOptionBool,                support_air_filtration))
    ((ConfigOptionBool,                accel_to_decel_enable))
    ((ConfigOptionPercent,             accel_to_decel_factor))
    ((ConfigOptionEnumsGeneric,        extruder_type))
    ((ConfigOptionEnumsGeneric,        nozzle_volume_type))
    ((ConfigOptionStrings,             extruder_ams_count))
    ((ConfigOptionInts,                printer_extruder_id))
    ((ConfigOptionInt,                 master_extruder_id))
    ((ConfigOptionStrings,             printer_extruder_variant))
    //Orca
    ((ConfigOptionBool,                has_scarf_joint_seam))
)

// This object is mapped to Perl as Slic3r::Config::Print.
PRINT_CONFIG_CLASS_DERIVED_DEFINE(
    PrintConfig,
    (MachineEnvelopeConfig, GCodeConfig),

    //BBS
    ((ConfigOptionInts,               additional_cooling_fan_speed))
    ((ConfigOptionBool,               reduce_crossing_wall))
    ((ConfigOptionBool,               z_direction_outwall_speed_continuous))
    ((ConfigOptionFloatOrPercent,     max_travel_detour_distance))
    ((ConfigOptionPoints,             printable_area))
    ((ConfigOptionPointsGroups,       extruder_printable_area))
    //BBS: add bed_exclude_area
    ((ConfigOptionPoints,             bed_exclude_area))
    ((ConfigOptionPoints,             head_wrap_detect_zone))
    // BBS
    ((ConfigOptionString,             bed_custom_texture))
    ((ConfigOptionString,             bed_custom_model))
    ((ConfigOptionEnum<BedType>,      curr_bed_type))
    ((ConfigOptionInts,               cool_plate_temp))
    ((ConfigOptionInts,               supertack_plate_temp))
    ((ConfigOptionInts,               eng_plate_temp))
    ((ConfigOptionInts,               hot_plate_temp)) // hot is short for high temperature
    ((ConfigOptionInts,               textured_plate_temp))
    ((ConfigOptionInts,               supertack_plate_temp_initial_layer))
    ((ConfigOptionInts,               cool_plate_temp_initial_layer))
    ((ConfigOptionInts,               eng_plate_temp_initial_layer))
    ((ConfigOptionInts,               hot_plate_temp_initial_layer)) // hot is short for high temperature
    ((ConfigOptionInts,               textured_plate_temp_initial_layer))
    ((ConfigOptionBools,              enable_overhang_bridge_fan))
    ((ConfigOptionInts,               overhang_fan_speed))
    ((ConfigOptionFloats,             pre_start_fan_time))
    ((ConfigOptionEnumsGeneric,       overhang_fan_threshold))
    ((ConfigOptionEnumsGeneric,       overhang_threshold_participating_cooling))
    ((ConfigOptionEnum<PrintSequence>,print_sequence))
    ((ConfigOptionInts,               first_layer_print_sequence))
    ((ConfigOptionInts,               other_layers_print_sequence))
    ((ConfigOptionInt,                other_layers_print_sequence_nums))
    ((ConfigOptionBools,              slow_down_for_layer_cooling))
    ((ConfigOptionFloatsNullable,     default_acceleration))
    ((ConfigOptionFloatsNullable,     travel_acceleration))
    ((ConfigOptionFloatsNullable,     initial_layer_travel_acceleration))
    ((ConfigOptionFloatsNullable,     inner_wall_acceleration))
    ((ConfigOptionFloatsOrPercentsNullable,   sparse_infill_acceleration))
    ((ConfigOptionBools,              activate_air_filtration))
    ((ConfigOptionInts,               during_print_exhaust_fan_speed))
    ((ConfigOptionInts,               complete_print_exhaust_fan_speed))
    ((ConfigOptionInts,               close_fan_the_first_x_layers))
    ((ConfigOptionEnum<DraftShield>,  draft_shield))
    ((ConfigOptionFloat,              extruder_clearance_height_to_rod))//BBs
    ((ConfigOptionFloat,              extruder_clearance_height_to_lid))//BBS
    ((ConfigOptionFloat,              extruder_clearance_dist_to_rod))
    ((ConfigOptionFloat,              nozzle_height))
    ((ConfigOptionFloat,              extruder_clearance_max_radius))
    ((ConfigOptionStrings,            extruder_colour))
    ((ConfigOptionPoints,             extruder_offset))
    ((ConfigOptionBools,              reduce_fan_stop_start_freq))
    ((ConfigOptionInts,               fan_cooling_layer_time))
    ((ConfigOptionFloatsNullable,     top_surface_acceleration))
    ((ConfigOptionFloatsNullable,     outer_wall_acceleration))
    ((ConfigOptionFloatsNullable,     initial_layer_acceleration))
    ((ConfigOptionFloat,              initial_layer_line_width))
    ((ConfigOptionFloat,              initial_layer_print_height))
    ((ConfigOptionFloatsNullable,     initial_layer_speed))
    //BBS
    ((ConfigOptionFloatsNullable,     initial_layer_infill_speed))
    ((ConfigOptionIntsNullable,       nozzle_temperature_initial_layer))
    ((ConfigOptionInts,               full_fan_speed_layer))
    ((ConfigOptionInts,               fan_max_speed))
    ((ConfigOptionFloatsNullable,     max_layer_height))
    ((ConfigOptionInts,               fan_min_speed))
    ((ConfigOptionFloatsNullable,     min_layer_height))
    ((ConfigOptionString,             printer_notes))
    ((ConfigOptionFloat,              printable_height))
    ((ConfigOptionFloatsNullable,     extruder_printable_height))
    ((ConfigOptionPoint,              best_object_pos))
    ((ConfigOptionFloats,             slow_down_min_speed))
    ((ConfigOptionFloatsNullable,     nozzle_diameter))
    ((ConfigOptionBool,               reduce_infill_retraction))
    ((ConfigOptionBool,               ooze_prevention))
    ((ConfigOptionString,             filename_format))
    ((ConfigOptionStrings,            post_process))
    ((ConfigOptionString,             printer_model))
    ((ConfigOptionString,             process_notes))
    ((ConfigOptionFloat,              resolution))
    ((ConfigOptionFloatsNullable,     retraction_minimum_travel))
    ((ConfigOptionBoolsNullable,      retract_when_changing_layer))
    ((ConfigOptionFloat,              skirt_distance))
    ((ConfigOptionInt,                skirt_height))
    ((ConfigOptionInt,                skirt_loops))
    ((ConfigOptionInts,               slow_down_layer_time))
    ((ConfigOptionBool,               spiral_mode))
    ((ConfigOptionBool,               spiral_mode_smooth))
    ((ConfigOptionFloatOrPercent,     spiral_mode_max_xy_smoothing))
    ((ConfigOptionInt,                standby_temperature_delta))
    ((ConfigOptionIntsNullable,       nozzle_temperature))
    ((ConfigOptionInts,               chamber_temperatures))
    ((ConfigOptionBoolsNullable,      wipe))
    // BBS
    ((ConfigOptionInts,               nozzle_temperature_range_low))
    ((ConfigOptionInts,               nozzle_temperature_range_high))
    ((ConfigOptionFloatsNullable,     wipe_distance))
    ((ConfigOptionBool,               enable_prime_tower))
    ((ConfigOptionBool,               prime_tower_enable_framework))
    // BBS: change wipe_tower_x and wipe_tower_y data type to floats to add partplate logic
    ((ConfigOptionFloats,             wipe_tower_x))
    ((ConfigOptionFloats,             wipe_tower_y))
    ((ConfigOptionFloat,              prime_tower_width))
    ((ConfigOptionFloat,              wipe_tower_per_color_wipe))
    ((ConfigOptionFloat,              wipe_tower_rotation_angle))
    ((ConfigOptionFloat,              prime_tower_brim_width))
    ((ConfigOptionFloat,              prime_tower_max_speed))
    ((ConfigOptionFloat,              prime_tower_extra_rib_length))
    ((ConfigOptionFloat,              prime_tower_rib_width))
    ((ConfigOptionPercent,            prime_tower_infill_gap))
    ((ConfigOptionBool,               prime_tower_skip_points))
    ((ConfigOptionBool,               prime_tower_flat_ironing))
    ((ConfigOptionBool,               prime_tower_rib_wall))
    ((ConfigOptionBool,               prime_tower_fillet_wall))
    //((ConfigOptionFloat,              wipe_tower_bridging))
    ((ConfigOptionFloats,             flush_volumes_matrix))
    ((ConfigOptionFloats,             flush_volumes_vector))
    // BBS: wipe tower is only used for priming
    ((ConfigOptionFloats,             flush_multiplier))
    //((ConfigOptionFloat,              z_offset))
    // BBS: project filaments
    ((ConfigOptionFloats,             filament_colour_new))
    // BBS: not in any preset, calculated before slicing
    ((ConfigOptionBool,               has_prime_tower))
    ((ConfigOptionFloatsNullable,     nozzle_volume))
    ((ConfigOptionPoints,             start_end_points))
    ((ConfigOptionEnum<TimelapseType>,    timelapse_type))
    ((ConfigOptionFloat,              default_jerk))
    ((ConfigOptionFloat,              outer_wall_jerk))
    ((ConfigOptionFloat,              inner_wall_jerk))
    ((ConfigOptionFloat,              infill_jerk))
    ((ConfigOptionFloat,              top_surface_jerk))
    ((ConfigOptionFloat,              initial_layer_jerk))
    ((ConfigOptionFloat,              travel_jerk))
    ((ConfigOptionBool,               is_infill_first))
    // BBS: move from PrintObjectConfig
    ((ConfigOptionBool,               independent_support_layer_height))
    ((ConfigOptionBool,               exclude_object))
    ((ConfigOptionPercents,            filament_shrink))
    ((ConfigOptionFloats,             grab_length))
    //BBS
    ((ConfigOptionFloats,             circle_compensation_speed))
    ((ConfigOptionFloats,             diameter_limit))
    ((ConfigOptionFloats,             counter_coef_1))
    ((ConfigOptionFloats,             counter_coef_2))
    ((ConfigOptionFloats,             counter_coef_3))
    ((ConfigOptionFloats,             hole_coef_1))
    ((ConfigOptionFloats,             hole_coef_2))
    ((ConfigOptionFloats,             hole_coef_3))
    ((ConfigOptionFloats,             counter_limit_min))
    ((ConfigOptionFloats,             counter_limit_max))
    ((ConfigOptionFloats,             hole_limit_min))
    ((ConfigOptionFloats,             hole_limit_max))
    ((ConfigOptionFloats,             filament_prime_volume)))
// This object is mapped to Perl as Slic3r::Config::Full.
PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
    FullPrintConfig,
    (PrintObjectConfig, PrintRegionConfig, PrintConfig)
)

// Validate the FullPrintConfig. Returns an empty string on success, otherwise an error message is returned.
std::map<std::string, std::string> validate(const FullPrintConfig &config, bool under_cli = false);

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintConfig,
    ((ConfigOptionString,     filename_format))
)

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintObjectConfig,

    ((ConfigOptionFloat, layer_height))

    //Number of the layers needed for the exposure time fade [3;20]
    ((ConfigOptionInt,  faded_layers))/*= 10*/

    ((ConfigOptionFloat, slice_closing_radius))

    // Enabling or disabling support creation
    ((ConfigOptionBool,  supports_enable))

    // Diameter in mm of the pointing side of the head.
    ((ConfigOptionFloat, support_head_front_diameter))/*= 0.2*/

    // How much the pinhead has to penetrate the model surface
    ((ConfigOptionFloat, support_head_penetration))/*= 0.2*/

    // Width in mm from the back sphere center to the front sphere center.
    ((ConfigOptionFloat, support_head_width))/*= 1.0*/

    // Radius in mm of the support pillars.
    ((ConfigOptionFloat, support_pillar_diameter))/*= 0.8*/

    // The percentage of smaller pillars compared to the normal pillar diameter
    // which are used in problematic areas where a normal pilla cannot fit.
    ((ConfigOptionPercent, support_small_pillar_diameter_percent))

    // How much bridge (supporting another pinhead) can be placed on a pillar.
    ((ConfigOptionInt,   support_max_bridges_on_pillar))

    // How the pillars are bridged together
    ((ConfigOptionEnum<SLAPillarConnectionMode>, support_pillar_connection_mode))

    // Generate only ground facing supports
    ((ConfigOptionBool, support_buildplate_only))

    // TODO: unimplemented at the moment. This coefficient will have an impact
    // when bridges and pillars are merged. The resulting pillar should be a bit
    // thicker than the ones merging into it. How much thicker? I don't know
    // but it will be derived from this value.
    ((ConfigOptionFloat, support_pillar_widening_factor))

    // Radius in mm of the pillar base.
    ((ConfigOptionFloat, support_base_diameter))/*= 2.0*/

    // The height of the pillar base cone in mm.
    ((ConfigOptionFloat, support_base_height))/*= 1.0*/

    // The minimum distance of the pillar base from the model in mm.
    ((ConfigOptionFloat, support_base_safety_distance)) /*= 1.0*/

    // The default angle for connecting support sticks and junctions.
    ((ConfigOptionFloat, support_critical_angle))/*= 45*/

    // The max length of a bridge in mm
    ((ConfigOptionFloat, support_max_bridge_length))/*= 15.0*/

    // The max distance of two pillars to get cross linked.
    ((ConfigOptionFloat, support_max_pillar_link_distance))

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom. Units in mm.
    ((ConfigOptionFloat, support_object_elevation))/*= 5.0*/

    /////// Following options influence automatic support points placement:
    ((ConfigOptionInt, support_points_density_relative))
    ((ConfigOptionFloat, support_points_minimal_distance))

    // Now for the base pool (pad) /////////////////////////////////////////////

    // Enabling or disabling support creation
    ((ConfigOptionBool,  pad_enable))

    // The thickness of the pad walls
    ((ConfigOptionFloat, pad_wall_thickness))/*= 2*/

    // The height of the pad from the bottom to the top not considering the pit
    ((ConfigOptionFloat, pad_wall_height))/*= 5*/

    // How far should the pad extend around the contained geometry
    ((ConfigOptionFloat, pad_brim_size))

    // The greatest distance where two individual pads are merged into one. The
    // distance is measured roughly from the centroids of the pads.
    ((ConfigOptionFloat, pad_max_merge_distance))/*= 50*/

    // The smoothing radius of the pad edges
    // ((ConfigOptionFloat, pad_edge_radius))/*= 1*/;

    // The slope of the pad wall...
    ((ConfigOptionFloat, pad_wall_slope))

    // /////////////////////////////////////////////////////////////////////////
    // Zero elevation mode parameters:
    //    - The object pad will be derived from the model geometry.
    //    - There will be a gap between the object pad and the generated pad
    //      according to the support_base_safety_distance parameter.
    //    - The two pads will be connected with tiny connector sticks
    // /////////////////////////////////////////////////////////////////////////

    // Disable the elevation (ignore its value) and use the zero elevation mode
    ((ConfigOptionBool, pad_around_object))

    ((ConfigOptionBool, pad_around_object_everywhere))

    // This is the gap between the object bottom and the generated pad
    ((ConfigOptionFloat, pad_object_gap))

    // How far to place the connector sticks on the object pad perimeter
    ((ConfigOptionFloat, pad_object_connector_stride))

    // The width of the connectors sticks
    ((ConfigOptionFloat, pad_object_connector_width))

    // How much should the tiny connectors penetrate into the model body
    ((ConfigOptionFloat, pad_object_connector_penetration))

    // /////////////////////////////////////////////////////////////////////////
    // Model hollowing parameters:
    //   - Models can be hollowed out as part of the SLA print process
    //   - Thickness of the hollowed model walls can be adjusted
    //   -
    //   - Additional holes will be drilled into the hollow model to allow for
    //   - resin removal.
    // /////////////////////////////////////////////////////////////////////////

    ((ConfigOptionBool, hollowing_enable))

    // The minimum thickness of the model walls to maintain. Note that the
    // resulting walls may be thicker due to smoothing out fine cavities where
    // resin could stuck.
    ((ConfigOptionFloat, hollowing_min_thickness))

    // Indirectly controls the voxel size (resolution) used by openvdb
    ((ConfigOptionFloat, hollowing_quality))

    // Indirectly controls the minimum size of created cavities.
    ((ConfigOptionFloat, hollowing_closing_distance))
)

enum SLAMaterialSpeed { slamsSlow, slamsFast };

PRINT_CONFIG_CLASS_DEFINE(
    SLAMaterialConfig,

    ((ConfigOptionFloat,                       initial_layer_height))
    ((ConfigOptionFloat,                       bottle_cost))
    ((ConfigOptionFloat,                       bottle_volume))
    ((ConfigOptionFloat,                       bottle_weight))
    ((ConfigOptionFloat,                       material_density))
    ((ConfigOptionFloat,                       exposure_time))
    ((ConfigOptionFloat,                       initial_exposure_time))
    ((ConfigOptionFloats,                      material_correction))
    ((ConfigOptionFloat,                       material_correction_x))
    ((ConfigOptionFloat,                       material_correction_y))
    ((ConfigOptionFloat,                       material_correction_z))
    ((ConfigOptionEnum<SLAMaterialSpeed>,      material_print_speed))
)

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrinterConfig,

    ((ConfigOptionEnum<PrinterTechnology>,    printer_technology))
    ((ConfigOptionPoints,                     printable_area))
    ((ConfigOptionFloat,                      printable_height))
    ((ConfigOptionFloat,                      display_width))
    ((ConfigOptionFloat,                      display_height))
    ((ConfigOptionInt,                        display_pixels_x))
    ((ConfigOptionInt,                        display_pixels_y))
    ((ConfigOptionEnum<SLADisplayOrientation>,display_orientation))
    ((ConfigOptionBool,                       display_mirror_x))
    ((ConfigOptionBool,                       display_mirror_y))
    ((ConfigOptionFloats,                     relative_correction))
    ((ConfigOptionFloat,                      relative_correction_x))
    ((ConfigOptionFloat,                      relative_correction_y))
    ((ConfigOptionFloat,                      relative_correction_z))
    ((ConfigOptionFloat,                      absolute_correction))
    ((ConfigOptionFloat,                      elefant_foot_compensation))
    ((ConfigOptionFloat,                      elefant_foot_min_width))
    ((ConfigOptionFloat,                      gamma_correction))
    ((ConfigOptionFloat,                      fast_tilt_time))
    ((ConfigOptionFloat,                      slow_tilt_time))
    ((ConfigOptionFloat,                      area_fill))
    ((ConfigOptionFloat,                      min_exposure_time))
    ((ConfigOptionFloat,                      max_exposure_time))
    ((ConfigOptionFloat,                      min_initial_exposure_time))
    ((ConfigOptionFloat,                      max_initial_exposure_time))
)

PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
    SLAFullPrintConfig,
    (SLAPrinterConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAMaterialConfig)
)

#undef STATIC_PRINT_CONFIG_CACHE
#undef STATIC_PRINT_CONFIG_CACHE_BASE
#undef STATIC_PRINT_CONFIG_CACHE_DERIVED
#undef PRINT_CONFIG_CLASS_ELEMENT_DEFINITION
#undef PRINT_CONFIG_CLASS_ELEMENT_EQUAL
#undef PRINT_CONFIG_CLASS_ELEMENT_LOWER
#undef PRINT_CONFIG_CLASS_ELEMENT_HASH
#undef PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION
#undef PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2
#undef PRINT_CONFIG_CLASS_DEFINE
#undef PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST
#undef PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE0
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE1
#undef PRINT_CONFIG_CLASS_DERIVED_HASH
#undef PRINT_CONFIG_CLASS_DERIVED_EQUAL
#undef PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM
#undef PRINT_CONFIG_CLASS_DERIVED_INITCACHE
#undef PRINT_CONFIG_CLASS_DERIVED_INITIALIZER
#undef PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM

class CLIActionsConfigDef : public ConfigDef
{
public:
    CLIActionsConfigDef();
};

class CLITransformConfigDef : public ConfigDef
{
public:
    CLITransformConfigDef();
};

class CLIMiscConfigDef : public ConfigDef
{
public:
    CLIMiscConfigDef();
};

// This class defines the command line options representing actions.
extern const CLIActionsConfigDef    cli_actions_config_def;

// This class defines the command line options representing transforms.
extern const CLITransformConfigDef  cli_transform_config_def;

// This class defines all command line options that are not actions or transforms.
extern const CLIMiscConfigDef       cli_misc_config_def;

class DynamicPrintAndCLIConfig : public DynamicPrintConfig
{
public:
    DynamicPrintAndCLIConfig() {}
    DynamicPrintAndCLIConfig(const DynamicPrintAndCLIConfig &other) : DynamicPrintConfig(other) {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*        def() const override { return &s_def; }

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                    handle_legacy(t_config_option_key &opt_key, std::string &value) const override;

private:
    class PrintAndCLIConfigDef : public ConfigDef
    {
    public:
        PrintAndCLIConfigDef() {
            this->options.insert(print_config_def.options.begin(), print_config_def.options.end());
            this->options.insert(cli_actions_config_def.options.begin(), cli_actions_config_def.options.end());
            this->options.insert(cli_transform_config_def.options.begin(), cli_transform_config_def.options.end());
            this->options.insert(cli_misc_config_def.options.begin(), cli_misc_config_def.options.end());
            for (const auto &kvp : this->options)
                this->by_serialization_key_ordinal[kvp.second.serialization_key_ordinal] = &kvp.second;
        }
        // Do not release the default values, they are handled by print_config_def & cli_actions_config_def / cli_transform_config_def / cli_misc_config_def.
        ~PrintAndCLIConfigDef() { this->options.clear(); }
    };
    static PrintAndCLIConfigDef s_def;
};

Polygon get_shared_poly(const std::vector<Pointfs>& extruder_polys);
Points get_bed_shape(const DynamicPrintConfig &cfg, bool use_share = true);
Points get_bed_shape(const PrintConfig &cfg, bool use_share = false);
Points get_bed_shape(const SLAPrinterConfig &cfg);
Slic3r::Polygon get_bed_shape_with_excluded_area(const PrintConfig& cfg, bool use_share = false);
bool has_skirt(const DynamicPrintConfig& cfg);
float get_real_skirt_dist(const DynamicPrintConfig& cfg);

// ModelConfig is a wrapper around DynamicPrintConfig with an addition of a timestamp.
// Each change of ModelConfig is tracked by assigning a new timestamp from a global counter.
// The counter is used for faster synchronization of the background slicing thread
// with the front end by skipping synchronization of equal config dictionaries.
// The global counter is also used for avoiding unnecessary serialization of config
// dictionaries when taking an Undo snapshot.
//
// The global counter is NOT thread safe, therefore it is recommended to use ModelConfig from
// the main thread only.
//
// As there is a global counter and it is being increased with each change to any ModelConfig,
// if two ModelConfig dictionaries differ, they should differ with their timestamp as well.
// Therefore copying the ModelConfig including its timestamp is safe as there is no harm
// in having multiple ModelConfig with equal timestamps as long as their dictionaries are equal.
//
// The timestamp is used by the Undo/Redo stack. As zero timestamp means invalid timestamp
// to the Undo/Redo stack (zero timestamp means the Undo/Redo stack needs to serialize and
// compare serialized data for differences), zero timestamp shall never be used.
// Timestamp==1 shall only be used for empty dictionaries.
class ModelConfig
{
public:
    // Following method clears the config and increases its timestamp, so the deleted
    // state is considered changed from perspective of the undo/redo stack.
    void         reset() { m_data.clear(); touch(); }

    void         assign_config(const ModelConfig &rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = rhs.m_data;
            m_timestamp = rhs.m_timestamp;
        }
    }
    void         assign_config(ModelConfig &&rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = std::move(rhs.m_data);
            m_timestamp = rhs.m_timestamp;
            rhs.reset();
        }
    }

    // Modification of the ModelConfig is not thread safe due to the global timestamp counter!
    // Don't call modification methods from the back-end!
    // Assign methods don't assign if src==dst to not having to bump the timestamp in case they are equal.
    void         assign_config(const DynamicPrintConfig &rhs)  { if (m_data != rhs) { m_data = rhs; this->touch(); } }
    void         assign_config(DynamicPrintConfig &&rhs)       { if (m_data != rhs) { m_data = std::move(rhs); this->touch(); } }
    void         apply(const ModelConfig &other, bool ignore_nonexistent = false) { this->apply(other.get(), ignore_nonexistent); }
    void         apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_data.apply_only(other, other.keys(), ignore_nonexistent); this->touch(); }
    void         apply_only(const ModelConfig &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { this->apply_only(other.get(), keys, ignore_nonexistent); }
    void         apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_data.apply_only(other, keys, ignore_nonexistent); this->touch(); }
    bool         set_key_value(const std::string &opt_key, ConfigOption *opt) { bool out = m_data.set_key_value(opt_key, opt); this->touch(); return out; }
    template<typename T>
    void         set(const std::string &opt_key, T value) { m_data.set(opt_key, value, true); this->touch(); }
    void         set_deserialize(const t_config_option_key &opt_key, const std::string &str, ConfigSubstitutionContext &substitution_context, bool append = false)
        { m_data.set_deserialize(opt_key, str, substitution_context, append); this->touch(); }
    bool         erase(const t_config_option_key &opt_key) { bool out = m_data.erase(opt_key); if (out) this->touch(); return out; }

    // Getters are thread safe.
    // The following implicit conversion breaks the Cereal serialization.
//    operator const DynamicPrintConfig&() const throw() { return this->get(); }
    const DynamicPrintConfig&   get() const throw() { return m_data; }
    bool                        empty() const throw() { return m_data.empty(); }
    size_t                      size() const throw() { return m_data.size(); }
    auto                        cbegin() const { return m_data.cbegin(); }
    auto                        cend() const { return m_data.cend(); }
    t_config_option_keys        keys() const { return m_data.keys(); }
    bool                        has(const t_config_option_key &opt_key) const { return m_data.has(opt_key); }
    const ConfigOption*         option(const t_config_option_key &opt_key) const { return m_data.option(opt_key); }
    int                         opt_int(const t_config_option_key &opt_key) const { return m_data.opt_int(opt_key); }
    int                         extruder() const { return opt_int("extruder"); }
    double                      opt_float(const t_config_option_key &opt_key) const { return m_data.opt_float(opt_key); }
    std::string                 opt_serialize(const t_config_option_key &opt_key) const { return m_data.opt_serialize(opt_key); }

    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    virtual uint64_t    timestamp() const throw() { return m_timestamp; }
    bool                timestamp_matches(const ModelConfig &rhs) const throw() { return m_timestamp == rhs.m_timestamp; }
    // Not thread safe! Should not be called from other than the main thread!
    void                touch() { m_timestamp = ++ s_last_timestamp; }

private:
    friend class cereal::access;
    template<class Archive> void serialize(Archive& ar) { ar(m_timestamp); ar(m_data); }

    uint64_t                    m_timestamp { 1 };
    DynamicPrintConfig          m_data;

    static uint64_t             s_last_timestamp;
};

// const std::vector<double> &fv_matrix:  origin matrix from json
// size_t extruder_id: -1 means single-nozzle for old file, 0 means the 1st extruder, 1 means the 2nd extruder
template<class T>
static std::vector<T> get_flush_volumes_matrix(const std::vector<T> &fv_matrix, size_t extruder_id = -1, size_t nozzle_nums = 1)
{
    if (extruder_id != -1 && nozzle_nums != 1) {
        return std::vector<T>(fv_matrix.begin() + size_t(fv_matrix.size() / nozzle_nums * extruder_id + EPSILON),
                                   fv_matrix.begin() + size_t(fv_matrix.size() / nozzle_nums * (extruder_id + 1) + EPSILON));
    }
    return fv_matrix;
}

// std::vector<double> &out_matrix:
// const std::vector<double> &fv_matrix: the matrix of one nozzle
// size_t extruder_id: -1 means single-nozzle for old file, 0 means the 1st extruder, 1 means the 2nd extruder
template<class T>
static void set_flush_volumes_matrix(std::vector<T> &out_matrix, const std::vector<T> &fv_matrix, size_t extruder_id = -1, size_t nozzle_nums = 1)
{
    bool is_multi_extruder = false;
    if (extruder_id != -1 && nozzle_nums != 1) {
        std::copy(fv_matrix.begin(), fv_matrix.end(), out_matrix.begin() + size_t(out_matrix.size() / nozzle_nums * extruder_id + EPSILON));
    }
    else {
        out_matrix = std::vector<T>(fv_matrix.begin(), fv_matrix.end());
    }
}

size_t get_extruder_index(const GCodeConfig& config, unsigned int filament_id);

} // namespace Slic3r

// Serialization through the Cereal library
namespace cereal {
    // Let cereal know that there are load / save non-member functions declared for DynamicPrintConfig, ignore serialize / load / save from parent class DynamicConfig.
    template <class Archive> struct specialize<Archive, Slic3r::DynamicPrintConfig, cereal::specialization::non_member_load_save> {};

    template<class Archive> void load(Archive& archive, Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt;
        archive(cnt);
        config.clear();
        for (size_t i = 0; i < cnt; ++ i) {
            size_t serialization_key_ordinal;
            archive(serialization_key_ordinal);
            assert(serialization_key_ordinal > 0);
            auto it = Slic3r::print_config_def.by_serialization_key_ordinal.find(serialization_key_ordinal);
            assert(it != Slic3r::print_config_def.by_serialization_key_ordinal.end());
            config.set_key_value(it->second->opt_key, it->second->load_option_from_archive(archive));
        }
    }

    template<class Archive> void save(Archive& archive, const Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt = config.size();
        archive(cnt);
        for (auto it = config.cbegin(); it != config.cend(); ++it) {
            const Slic3r::ConfigOptionDef* optdef = Slic3r::print_config_def.get(it->first);
            assert(optdef != nullptr);
            assert(optdef->serialization_key_ordinal > 0);
            archive(optdef->serialization_key_ordinal);
            optdef->save_option_to_archive(archive, it->second.get());
        }
    }
}

#endif

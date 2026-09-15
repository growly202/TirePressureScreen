/**
 * @file UI_design_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "UI_design_gen.h"

#if LV_USE_XML
#endif /* LV_USE_XML */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void check_font(lv_font_t ** font, const char * name);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t UI_design_target = UI_DESIGN_TARGET_ALL;

/*----------------
 * Translations
 *----------------*/

#ifndef LV_EDITOR_PREVIEW
    static const char * translation_languages[] = {"en", "de", NULL};
    static const char * translation_tags[] = {"dog", "cat", "house", NULL};
    static const char * translation_texts[] = {
        "This is a dog", "Das ist ein Hund", /* dog */
        "A curious little cat", "Eine neugierige kleine Katze", /* cat */
        "The house is cozy and warm", "Das Haus ist gemütlich und warm", /* house */
    };
#endif

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*--------------------
 *  Permanent screens
 *-------------------*/

/*----------------
 * Fonts
 *----------------*/

lv_font_t * font_label_18;
extern lv_font_t font_label_18_data;
lv_font_t * font_unit_22;
extern lv_font_t font_unit_22_data;
lv_font_t * font_pressure_52;
extern lv_font_t font_pressure_52_data;

/*----------------
 * Images
 *----------------*/

/* Targets: any */
const void * car_top_image = NULL;
extern const lv_image_dsc_t car_top_image_data;

/*----------------
 * Global styles
 *----------------*/

/*----------------
 * Subjects
 *----------------*/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void UI_design_init_gen(const char * asset_path)
{
    char buf[256];


    /*----------------
     * Fonts
     *----------------*/

    /* Targets: any */

    #if UI_DESIGN_CHECK_COMPILE_TARGET(UI_DESIGN_TARGET_ALL)
    if (UI_design_check_target(UI_DESIGN_TARGET_ALL)) {
        if (!font_label_18) {
            /* font_label_18 */
            /* get font 'font_label_18' from a C array */
            font_label_18 = &font_label_18_data;

        }
        if (!font_unit_22) {
            /* font_unit_22 */
            /* get font 'font_unit_22' from a C array */
            font_unit_22 = &font_unit_22_data;

        }
        if (!font_pressure_52) {
            /* font_pressure_52 */
            /* get font 'font_pressure_52' from a C array */
            font_pressure_52 = &font_pressure_52_data;

        }
    }
    #endif

    /*----------------
     * Images
     *----------------*/

    /* Targets: any */
    #if UI_DESIGN_CHECK_COMPILE_TARGET(UI_DESIGN_TARGET_ALL)
    if (UI_design_check_target(UI_DESIGN_TARGET_ALL)) {
        /* car_top_image */
        if (!car_top_image) {
            car_top_image = &car_top_image_data;
        }
    }
    #endif

    /*----------------
     * Global styles
     *----------------*/

    /*----------------
     * Subjects
     *----------------*/
    /*----------------
     * Translations
     *----------------*/

    #ifndef LV_EDITOR_PREVIEW
        lv_translation_add_static(translation_languages, translation_tags, translation_texts);
        lv_translation_set_language(translation_languages[0]);
    #endif

#if LV_USE_XML
    /* Register widgets */

    /* Check all fonts / default if needed. This prevents fonts that are used in one target but
       defined in another from causing assertion failures during rendering of the Preview. */
    check_font(&font_label_18, "font_label_18");
    check_font(&font_unit_22, "font_unit_22");
    check_font(&font_pressure_52, "font_pressure_52");

    /* Register fonts */
    lv_xml_register_font(NULL, "font_label_18", font_label_18);
    lv_xml_register_font(NULL, "font_unit_22", font_unit_22);
    lv_xml_register_font(NULL, "font_pressure_52", font_pressure_52);

    /* Register subjects */

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
    lv_xml_register_image(NULL, "car_top_image", car_top_image);
#endif

#if LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manually from XML using lv_xml_create() */
#endif
}

void UI_design_set_target(uint32_t target)
{
    UI_design_target = target;
}

uint32_t UI_design_get_target(void)
{
    return UI_design_target;
}

bool UI_design_check_target(uint32_t target)
{
    return (UI_design_target & target) ? true : false;
}

/* Callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void check_font(lv_font_t ** font, const char * name)
{
    if (!(*font)) {
        *font = (lv_font_t *)LV_FONT_DEFAULT;
        LV_LOG_WARN("font `%s` was not set. Using `LV_FONT_DEFAULT` instead", name);
    }
}

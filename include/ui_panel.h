#ifndef UI_PANEL_H
#define UI_PANEL_H

typedef struct {
  lv_obj_t *screen;
  dhash     hash;
} UIPanel;


typedef struct {
  dhash hash;
} UIStyles;



typedef struct {
  dhash hash;
} UIWidgetRegistry;


typedef struct {
  void (*update_widget)(lv_obj_t *widget, uint32_t prop);
} UIWidgetEntry;


typedef struct UIReactWidgetNode {
  struct UIReactWidgetNode *next;
  lv_obj_t *widget;
} UIReactWidgetNode;


typedef struct {
  dhash hash;
} UIReactWidgets;



#ifdef __cplusplus
extern "C" {
#endif

bool ui_panel_init(UIPanel *panel);
void ui_panel_free(UIPanel *panel);
bool ui_panel_add_obj(UIPanel *panel, uint32_t id, lv_obj_t *obj);
lv_obj_t *ui_panel_get_obj(UIPanel *panel, uint32_t id);

bool ui_styles_init(UIStyles *styles);
void ui_styles_free(UIStyles *styles);
bool ui_styles_add(UIStyles *styles, uint32_t id, lv_style_t *style);
lv_style_t *ui_styles_get(UIStyles *styles, uint32_t id);

bool ui_widget_reg_init(UIWidgetRegistry *wreg);
void ui_widget_reg_free(UIWidgetRegistry *wreg);
bool ui_widget_reg_add(UIWidgetRegistry *wreg, const lv_obj_class_t *obj_class, UIWidgetEntry *entry);
bool ui_widget_reg_get(UIWidgetRegistry *wreg, const lv_obj_class_t *obj_class, UIWidgetEntry *entry);
void ui_widget_reg_add_defaults(UIWidgetRegistry *wreg);

bool ui_react_widgets_init(UIReactWidgets *rw);
void ui_react_widgets_free(UIReactWidgets *rw);
bool ui_react_widgets_bind(UIReactWidgets *rw, uint32_t prop, lv_obj_t *widget);
UIReactWidgetNode *ui_react_widgets_get(UIReactWidgets *rw, uint32_t prop);
void ui_react_widgets_update(UIReactWidgets *rw, UIWidgetRegistry *wreg, uint32_t prop);

bool ui_panel_push(UIPanel *panel);
UIPanel *ui_panel_pop(void);
UIPanel *ui_panel_top(void);

#ifdef __cplusplus
}
#endif

#endif // UI_PANEL_H

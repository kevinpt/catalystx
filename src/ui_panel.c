#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "util/dhash.h"
#include "util/mempool.h"
#include "lvgl/lvgl.h"
#include "cstone/prop_id.h"
#include "cstone/prop_db.h"
#include "util/list_ops.h"
#include "ui_panel.h"


extern PropDB g_prop_db;
extern mpPoolSet g_pool_set;


// ******************** UI panel ********************

static void panel_item_destroy(dhKey key, void *value, void *ctx) {
  lv_obj_t *obj = (lv_obj_t *)value;

  lv_obj_del(obj);
}

bool ui_panel_init(UIPanel *panel) {
  // Setup hash table for widget prop: lv_obj_t * pairs
  dhConfig hash_cfg = {
    .init_buckets = 8,
    .value_size   = sizeof(lv_obj_t *),
    .max_storage  = 0,
    .ext_storage  = NULL,
    .destroy_item = panel_item_destroy,
    .replace_item = NULL,
    .gen_hash     = dh_gen_hash_int,
    .is_equal     = dh_equal_hash_keys_int
  };

  return dh_init(&panel->hash, &hash_cfg, panel);
}

void ui_panel_free(UIPanel *panel) {
  dh_free(&panel->hash);
}


bool ui_panel_add_obj(UIPanel *panel, uint32_t id, lv_obj_t *obj) {
  dhKey key = {
    .data = (void *)(uintptr_t)id,
    .length = sizeof(uint32_t)
  };

  return dh_insert(&panel->hash, key, &obj);
}


lv_obj_t *ui_panel_get_obj(UIPanel *panel, uint32_t id) {
  lv_obj_t *obj = NULL;
  dhKey key = {
    .data = (void *)(uintptr_t)id,
    .length = sizeof(uint32_t)
  };

  if(!dh_lookup(&panel->hash, key, &obj))
    return NULL;

  return obj;
}




// ******************** UI styles ********************

static void style_item_destroy(dhKey key, void *value, void *ctx) {
//  UIStyles *styles = (UIStyles *)ctx;
  lv_style_t *style = (lv_style_t *)value;

  lv_style_reset(style);
  free(style); // Alloc in app_styles_init()
}


bool ui_styles_init(UIStyles *styles) {
  // Setup hash table for style prop: lv_style_t pairs
  dhConfig hash_cfg = {
    .init_buckets = 8,
    .value_size   = sizeof(lv_style_t *),
    .max_storage  = 0,
    .ext_storage  = NULL,
    .destroy_item = style_item_destroy,
    .replace_item = NULL,
    .gen_hash     = dh_gen_hash_int,
    .is_equal     = dh_equal_hash_keys_int
  };

  return dh_init(&styles->hash, &hash_cfg, styles);
}

void ui_styles_free(UIStyles *styles) {
  dh_free(&styles->hash);
}


bool ui_styles_add(UIStyles *styles, uint32_t id, lv_style_t *style) {
  dhKey key = {
    .data = (void *)(uintptr_t)id,
    .length = sizeof(uint32_t)
  };

  return dh_insert(&styles->hash, key, &style);
}


lv_style_t *ui_styles_get(UIStyles *styles, uint32_t id) {
  lv_style_t *style = NULL;
  dhKey key = {
    .data = (void *)(uintptr_t)id,
    .length = sizeof(uint32_t)
  };

  if(!dh_lookup(&styles->hash, key, &style))
    return NULL;

  return style;
}


// ******************** Widget registry ********************

static void widget_reg_item_destroy(dhKey key, void *value, void *ctx) {
  //UIWidgetEntry *entry = (UIWidgetEntry *)value;
}


bool ui_widget_reg_init(UIWidgetRegistry *wreg) {
  // Setup hash table for prop: UIReactWidgetNode pairs
  dhConfig hash_cfg = {
    .init_buckets = 8,
    .value_size   = sizeof(UIWidgetEntry),
    .max_storage  = 0,
    .ext_storage  = NULL,
    .destroy_item = widget_reg_item_destroy,
    .replace_item = NULL,
    .gen_hash     = dh_gen_hash_int,  // Hash lv_obj_class_t as const integer ID
    .is_equal     = dh_equal_hash_keys_int
  };

  return dh_init(&wreg->hash, &hash_cfg, wreg);
}


void ui_widget_reg_free(UIWidgetRegistry *wreg) {
  dh_free(&wreg->hash);
}


bool ui_widget_reg_add(UIWidgetRegistry *wreg, const lv_obj_class_t *obj_class, UIWidgetEntry *entry) {
  dhKey key = {
    .data = obj_class,
    .length = sizeof(obj_class)
  };

  return dh_insert(&wreg->hash, key, entry);
}


bool ui_widget_reg_get(UIWidgetRegistry *wreg, const lv_obj_class_t *obj_class, UIWidgetEntry *entry) {
  dhKey key = {
    .data = obj_class,
    .length = sizeof(obj_class)
  };

  return dh_lookup(&wreg->hash, key, entry);
}


static void update_widget_switch(lv_obj_t *widget, uint32_t prop) {
  PropDBEntry value;
  prop_get(&g_prop_db, prop, &value);

  // Change switch state (Does not trigger event handler)
  if(value.value)
    lv_obj_add_state(widget, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(widget, LV_STATE_CHECKED);
}


void ui_widget_reg_add_defaults(UIWidgetRegistry *wreg) {
  UIWidgetEntry entry;
  entry.update_widget = update_widget_switch;
  ui_widget_reg_add(wreg, &lv_switch_class, &entry);

//  printf("## CLASS: %p\n", &lv_switch_class);
}


// ******************** Reactive widgets ********************

static void react_widgets_item_destroy(dhKey key, void *value, void *ctx) {
  UIReactWidgetNode *cur = (UIReactWidgetNode *)value;
  UIReactWidgetNode *next;

  while(cur) {
    next = cur->next;
    mp_free(&g_pool_set, cur);  // Alloc in ui_react_widgets_bind()
    cur = next;
  }
}


bool ui_react_widgets_init(UIReactWidgets *rw) {
  // Setup hash table for prop: UIReactWidgetNode pairs
  dhConfig hash_cfg = {
    .init_buckets = 8,
    .value_size   = sizeof(UIReactWidgetNode *),
    .max_storage  = 0,
    .ext_storage  = NULL,
    .destroy_item = react_widgets_item_destroy,
    .replace_item = NULL,
    .gen_hash     = dh_gen_hash_int,
    .is_equal     = dh_equal_hash_keys_int
  };

  return dh_init(&rw->hash, &hash_cfg, rw);
}


void ui_react_widgets_free(UIReactWidgets *rw) {
  dh_free(&rw->hash);
}


UIReactWidgetNode *ui_react_widgets_get(UIReactWidgets *rw, uint32_t prop) {
  UIReactWidgetNode *node = NULL;
  dhKey key = {
    .data = (void *)(uintptr_t)prop,
    .length = sizeof(prop)
  };

//  puts("## RW LOOKUP");

  if(!dh_lookup(&rw->hash, key, &node))
    return NULL;

  return node;
}


bool ui_react_widgets_bind(UIReactWidgets *rw, uint32_t prop, lv_obj_t *widget) {
  UIReactWidgetNode *node;

  // Check if widget is already bound to this prop
  node = ui_react_widgets_get(rw, prop);
  while(node) {
    if(node->widget == widget)
      return true;
    node = node->next;
  }

  bool status = false;

  // Lookup any existing binding for this prop
  node = ui_react_widgets_get(rw, prop);
  if(node) {  // Prop has binding
    UIReactWidgetNode *new_node = mp_alloc(&g_pool_set, sizeof(UIReactWidgetNode), NULL);
    if(new_node) {
      new_node->widget = widget;
      // Insert after first widget node so we don't have to change hash table
      ll_slist_add_after(&node, new_node);

//      printf("## RW BIND 2: %p\n", new_node);
    }

  } else { // New binding for prop
    node = mp_alloc(&g_pool_set, sizeof(*node), NULL);
    if(node) {
      dhKey key = {
        .data = (void *)(uintptr_t)prop,
        .length = sizeof(prop)
      };

      node->next = NULL;
      node->widget = widget;
      status = dh_insert(&rw->hash, key, &node);
//      printf("## RW BIND: %p\n", node);
    }
  }

  return status;
}


void ui_react_widgets_update(UIReactWidgets *rw, UIWidgetRegistry *wreg, uint32_t prop) {
  UIReactWidgetNode *node = ui_react_widgets_get(rw, prop);

//  printf("## REACT: P%08lX  %p\n", prop, node);

  while(node) {
    const lv_obj_class_t *obj_class = lv_obj_get_class(node->widget);

    // Lookup widget type in registry
    UIWidgetEntry entry;
    if(ui_widget_reg_get(wreg, obj_class, &entry)) {
//      printf("   W REG: %p", entry.update_widget);
      entry.update_widget(node->widget, prop);
    }

    node = node->next;
  }

}


// ******************** Panel stack ********************

typedef struct UIPanelNode {
  struct UIPanelNode *next;
  UIPanel *panel;
} UIPanelNode;

static UIPanelNode *s_panel_stack = NULL;


bool ui_panel_push(UIPanel *panel) {
  UIPanelNode *node = mp_alloc(&g_pool_set, sizeof(UIPanelNode), NULL);
  if(!node) return false;

  node->panel = panel;
  ll_slist_push(&s_panel_stack, node);

  lv_scr_load(panel->screen);

  return true;
}


UIPanel *ui_panel_pop(void) {
  // Pop top panel
  UIPanelNode *node = LL_NODE(ll_slist_pop(&s_panel_stack), UIPanelNode, next);
  if(!node) return NULL;

  UIPanel *panel = node->panel;
  mp_free(&g_pool_set, node);

  // Load new top panel
  if(s_panel_stack)
    lv_scr_load(s_panel_stack->panel->screen);

  return panel;
}


UIPanel *ui_panel_top(void) {
  if(s_panel_stack)
    return s_panel_stack->panel;
  else
    return NULL;
}


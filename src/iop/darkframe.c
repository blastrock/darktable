/*
    This file is part of darktable,
    Copyright (C) 2011-2020 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "bauhaus/bauhaus.h"
#include "common/imagebuf.h"
#include "control/control.h"
#include "develop/imageop.h"
#include "develop/imageop_math.h"
#include "develop/imageop_gui.h"
#include "dtgtk/resetlabel.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "iop/iop_api.h"

#include <gtk/gtk.h>
#include <stdlib.h>

DT_MODULE_INTROSPECTION(1, dt_iop_darkframe_params_t)

typedef struct dt_iop_darkframe_params_t
{
  char path[1024];
} dt_iop_darkframe_params_t;

typedef struct dt_iop_darkframe_gui_data_t
{
  GtkWidget *path;
  GtkWidget *load;
} dt_iop_darkframe_gui_data_t;

typedef struct dt_iop_darkframe_data_t
{
  char path[1024];
} dt_iop_darkframe_data_t;


const char *name()
{
  return _("dark frame subtraction");
}

// TODO I have copy pasted all this from hotpixels.c which is similar in purpose
// to this module, but I have no idea what all these mean
const char **description(struct dt_iop_module_t *self)
{
  return dt_iop_set_description(self, _("reduce noise by subtracting a dark frame"),
                                      _("corrective"),
                                      _("linear, raw, scene-referred"),
                                      _("reconstruction, raw"),
                                      _("linear, raw, scene-referred"));
}


int default_group()
{
  return IOP_GROUP_CORRECT | IOP_GROUP_TECHNICAL;
}

int flags()
{
  return IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_ONE_INSTANCE;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return IOP_CS_RAW;
}

void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid,
             void *const ovoid, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_darkframe_gui_data_t *g = (dt_iop_darkframe_gui_data_t *)self->gui_data;
  const dt_iop_darkframe_data_t *data = (dt_iop_darkframe_data_t *)piece->data;

  dt_image_t dark_frame;
  dt_mipmap_buffer_t buf;

  // TODO replace this -1
  dt_cache_entry_t *entry =  dt_cache_get(&darktable.mipmap_cache->mip_full.cache, -1, 'r');
  buf.cache_entry = entry;
  buf.imgid = -1;
  buf.size = DT_MIPMAP_FULL;
  buf.buf = 0;
  buf.width = buf.height = 0;
  buf.iscale = 0.0f;
  buf.color_space = DT_COLORSPACE_NONE;
  dt_imageio_retval_t ret = dt_imageio_open(&dark_frame, data->path, &buf);

  // TODO check that the resolution etc are the same
  if(ret == DT_IMAGEIO_OK)
  {
    fprintf(stderr, "opened!\n");
  }
  else
  {
    fprintf(stderr, "failed!\n");
    dt_cache_release(&darktable.mipmap_cache->mip_full.cache, entry);
    return;
  }

  if(piece->dsc_in.datatype != TYPE_UINT16)
  {
    fprintf(stderr, "unsupported data type!\n");
    dt_cache_release(&darktable.mipmap_cache->mip_full.cache, entry);
    return;
  }

  if(piece->buf_in.width != dark_frame.width || piece->buf_in.height != dark_frame.height)
  {
    fprintf(stderr, "wrong size %dx%d, possibly a thumbnail\n", piece->buf_in.width, piece->buf_in.height);
    dt_cache_release(&darktable.mipmap_cache->mip_full.cache, entry);
    return;
  }

// setup pipeline at image.c:726

  fprintf(stderr, "processing dark frame...\n");

  const uint16_t *const restrict in = (uint16_t*)ivoid;
  uint16_t *const restrict out = (uint16_t*)ovoid;
  uint16_t *const restrict df = (uint16_t*)buf.buf;
  const size_t df_start = roi_out->x + roi_out->y * dark_frame.width;
#ifdef _OPENMP
// #pragma omp parallel for simd default(none) \
//   dt_omp_firstprivate(ch, npixels, in, out, df)  \
//   schedule(simd:static) aligned(in, out : 64)
#endif
  for(int j = 0; j < roi_out->height; j++)
  {
    for(int i = 0; i < roi_out->width; i++)
    {
      const size_t pdf = (size_t)j * dark_frame.width + i + df_start;
      const size_t pout = (size_t)j * roi_out->width + i;
      const size_t pin = (size_t)j * roi_in->width + i;
      const size_t black_level = 600;
      out[pout] = MAX(((int32_t)in[pin]) - ((int32_t)(df[pdf] - black_level)), 0);
    }
  }

  dt_cache_release(&darktable.mipmap_cache->mip_full.cache, entry);
}

void reload_defaults(dt_iop_module_t *module)
{
  const dt_image_t *img = &module->dev->image_storage;
  const gboolean enabled = dt_image_is_raw(img) && !dt_image_is_monochrome(img);
  // can't be switched on for non-raw images:
  module->hide_enable_button = !enabled;
}

void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_darkframe_params_t *p = (dt_iop_darkframe_params_t *)params;
  dt_iop_darkframe_data_t *d = (dt_iop_darkframe_data_t *)piece->data;

  memset(d->path, 0, sizeof(d->path));
  g_strlcpy(d->path, p->path, sizeof(d->path));

  const dt_image_t *img = &pipe->image;
  const gboolean enabled = dt_image_is_raw(img) && !dt_image_is_monochrome(img);

  if(!enabled) piece->enabled = 0;
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_darkframe_data_t));
  dt_iop_darkframe_data_t *d = (dt_iop_darkframe_data_t *)piece->data;
  memset(d->path, 0, sizeof(d->path));
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
  piece->data = NULL;
}

static void path_callback(GtkWidget *entry, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(darktable.gui->reset) return;
  dt_iop_darkframe_params_t *p = (dt_iop_darkframe_params_t *)self->params;
  g_strlcpy(p->path, gtk_entry_get_text(GTK_ENTRY(entry)), sizeof(p->path));
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

void gui_update(dt_iop_module_t *self)
{
  dt_iop_darkframe_gui_data_t *g = (dt_iop_darkframe_gui_data_t *)self->gui_data;
  dt_iop_darkframe_params_t *p = (dt_iop_darkframe_params_t *)self->params;

  gtk_entry_set_text(GTK_ENTRY(g->path), p->path);

  const dt_image_t *img = &self->dev->image_storage;
  const gboolean enabled = dt_image_is_raw(img) && !dt_image_is_monochrome(img);
  // can't be switched on for non-raw images:
  self->hide_enable_button = !enabled;

  gtk_stack_set_visible_child_name(GTK_STACK(self->widget), self->hide_enable_button ? "non_raw" : "raw");
}

void gui_init(dt_iop_module_t *self)
{
  dt_iop_darkframe_gui_data_t *g = IOP_GUI_ALLOC(darkframe);

  GtkWidget *box_raw = self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);

  // path
  GtkWidget* label = dt_ui_label_new(_("path"));
  g->path = gtk_entry_new();
  gtk_entry_set_width_chars(GTK_ENTRY(g->path), 1);
  gtk_widget_set_tooltip_text(g->path, _("path to the raw dark frame"));
  g_signal_connect(G_OBJECT(g->path), "changed", G_CALLBACK(path_callback), self);

  GtkWidget *hbox = self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(label), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(g->path), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(box_raw), hbox, TRUE, TRUE, 0);

  // start building top level widget
  self->widget = gtk_stack_new();
  gtk_stack_set_homogeneous(GTK_STACK(self->widget), FALSE);

  GtkWidget *label_non_raw = dt_ui_label_new(_("dark frame subtraction\nonly works for raw images."));

  gtk_stack_add_named(GTK_STACK(self->widget), label_non_raw, "non_raw");
  gtk_stack_add_named(GTK_STACK(self->widget), box_raw, "raw");
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on


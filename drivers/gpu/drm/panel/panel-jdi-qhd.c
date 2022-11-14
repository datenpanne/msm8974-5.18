// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA Corporation
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

static const char * const regulator_names[] = {
	"vddp",
	"iovcc"
};

struct jdi_panel {
	struct device *dev;
	struct drm_panel base;
	/* the datasheet refers to them as DSI-left and DSI-right */
	struct mipi_dsi_device *left;
	struct mipi_dsi_device *right;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];

	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *lcd_mbist;
	struct gpio_desc *disp_lcd_enable;

	struct backlight_device *backlight;

	bool prepared;
	bool enabled;

	const struct drm_display_mode *mode;
};

static inline struct jdi_panel *to_jdi_panel(struct drm_panel *panel)
{
	return container_of(panel, struct jdi_panel, base);
}

static void jdi_wait_frames(struct jdi_panel *jdi, unsigned int frames)
{
	unsigned int refresh = drm_mode_vrefresh(jdi->mode);

	if (WARN_ON(frames > refresh))
		return;

	msleep(1000 / (refresh / frames));
}

static int jdi_panel_write(struct jdi_panel *jdi, u16 offset, u8 value)
{
	u8 payload[3] = { offset >> 8, offset & 0xff, value };
	struct mipi_dsi_device *dsi = jdi->left;
	ssize_t err;

	err = mipi_dsi_generic_write(dsi, payload, sizeof(payload));
	if (err < 0) {
		dev_err(&dsi->dev, "failed to write %02x to %04x: %zd\n",
			value, offset, err);
		return err;
	}

	err = mipi_dsi_dcs_nop(dsi);
	if (err < 0) {
		dev_err(&dsi->dev, "failed to send DCS nop: %zd\n", err);
		return err;
	}

	usleep_range(10, 20);

	return 0;
}

static __maybe_unused int jdi_panel_read(struct jdi_panel *jdi,
					   u16 offset, u8 *value)
{
	ssize_t err;

	cpu_to_be16s(&offset);

	err = mipi_dsi_generic_read(jdi->left, &offset, sizeof(offset),
				    value, sizeof(*value));
	if (err < 0)
		dev_err(&jdi->left->dev, "failed to read from %04x: %zd\n",
			offset, err);

	return err;
}

static int jdi_panel_init(struct jdi_panel *jdi)
{
	//struct jdi_panel *jdi = to_jdi_panel(panel);
//{
	//struct mipi_dsi_device *dsi = jdi->left;
	//struct device *dev = &jdi->dsi->dev;
	int err;

	jdi->left->mode_flags |= MIPI_DSI_MODE_LPM;
	jdi->right->mode_flags |= MIPI_DSI_MODE_LPM;
	
	err = mipi_dsi_dcs_soft_reset(jdi->left);
	if (err < 0)
		return err;

	/*err = mipi_dsi_generic_write(jdi->left, (u8[]){0x01}, 1);
	if (err < 0) {
		dev_err(panel->dev, "failed to set mcap: %d\n", err);
		return err;
	}*/

	usleep_range(10000, 20000);
        err = mipi_dsi_dcs_set_pixel_format(jdi->left, MIPI_DCS_PIXEL_FMT_24BIT << 4);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set pixel format: %d\n", err);
		return err;
	}

	/*err = mipi_dsi_dcs_set_column_address(jdi->left, 0, jdi->mode->hdisplay - 1);
	if (err < 0) {
		dev_err(panel->dev, "failed to set column address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(jdi->left, 0, jdi->mode->vdisplay - 1);
	if (err < 0) {
		dev_err(panel->dev, "failed to set page address: %d\n", err);
		return err;
	}*/

	/*
	 * BIT(5) BCTRL = 1 Backlight Control Block On, Brightness registers
	 *                  are active
	 * BIT(3) BL = 1    Backlight Control On
	 * BIT(2) DD = 0    Display Dimming is Off
	 */
	/*err = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				 (u8[]){ 0x24 }, 1);
	if (err < 0) {
		dev_err(dev, "failed to write control display: %d\n", err);
		return err;
	}*/

	/* CABC off */
	/*err = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_POWER_SAVE,
				 (u8[]){ 0x00 }, 1);
	if (err < 0) {
		dev_err(dev, "failed to set cabc off: %d\n", err);
		return err;
	}*/

	err = mipi_dsi_dcs_set_tear_on(jdi->left, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (err < 0) {
		dev_err(jdi->dev, "Failed to set tear on: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_tear_scanline(jdi->left, 0);
	if (err < 0) {
		dev_err(jdi->dev, "Failed to set tear scanline: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_exit_sleep_mode(jdi->left);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set exit sleep mode: %d\n", err);
		return err;
	}

	msleep(120);

	err = mipi_dsi_generic_write(jdi->left, (u8[]){0xB0, 0x00}, 2);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set mcap: %d\n", err);
		return err;
	}

	mdelay(10);

	/* Interface setting */
	err = mipi_dsi_generic_write(jdi->left, (u8[]){0xB3, 0x14}, 2);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set display interface setting: %d\n"
			, err);
		return err;
	}

	mdelay(20);

	err = mipi_dsi_generic_write(jdi->left, (u8[]){0xB0, 0x03}, 2);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set default values for mcap: %d\n"
			, err);
		return err;
	}

	return 0;
}


static int jdi_panel_post_on(struct jdi_panel *jdi)
{
	int err;
	
	err = mipi_dsi_generic_write(jdi->left, (u8[]){0xB0, 0x00}, 2);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set mcap: %d\n", err);
		return err;
	}

	mdelay(10);

	/* Interface setting */
	err = mipi_dsi_generic_write(jdi->left, (u8[]){0xB3, 0xc3}, 2);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set display interface setting: %d\n"
			, err);
		return err;
	}

	mdelay(20);

	err = mipi_dsi_generic_write(jdi->left, (u8[]){0xB0, 0x03}, 2);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set default values for mcap: %d\n"
			, err);
		return err;
	}
	return 0;	
}

static int jdi_panel_disable(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);

	if (!jdi->enabled)
		return 0;

	jdi->enabled = false;

	return 0;
}

static int jdi_panel_unprepare(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	int err;

	if (!jdi->prepared)
		return 0;

	jdi_wait_frames(jdi, 4);

	err = mipi_dsi_dcs_set_display_off(jdi->left);
	if (err < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", err);

	err = mipi_dsi_dcs_enter_sleep_mode(jdi->left);
	if (err < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", err);

	msleep(120);
	
	err = regulator_bulk_disable(ARRAY_SIZE(jdi->supplies), jdi->supplies);
	if (err < 0)
		dev_err(panel->dev, "regulator disable failed, %d\n", err);
		
	//regulator_disable(jdi->supply);
	
	gpiod_set_value(jdi->enable_gpio, 0);

	gpiod_set_value(jdi->reset_gpio, 1);

	gpiod_set_value(jdi->disp_lcd_enable, 0);
	
	jdi->prepared = false;

	return 0;
}

static int jdi_setup_symmetrical_split(struct mipi_dsi_device *left,
					 struct mipi_dsi_device *right,
					 const struct drm_display_mode *mode)
{
	int err;

	err = mipi_dsi_dcs_set_column_address(left, 0, mode->hdisplay / 2 - 1);
	if (err < 0) {
		dev_err(&left->dev, "failed to set column address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(left, 0, mode->vdisplay - 1);
	if (err < 0) {
		dev_err(&left->dev, "failed to set page address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_column_address(right, mode->hdisplay / 2,
					      mode->hdisplay - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set column address: %d\n", err);
		return err;
	}
	
	err = mipi_dsi_dcs_set_column_address(right, mode->hdisplay / 2,
					      mode->hdisplay - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set column address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(right, 0, mode->vdisplay - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set page address: %d\n", err);
		return err;
	}

	return 0;
}

static int jdi_panel_prepare(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	u8 format = MIPI_DCS_PIXEL_FMT_24BIT;
	int err;

	if (jdi->prepared)
		return 0;
		
	err = regulator_bulk_enable(ARRAY_SIZE(jdi->supplies), jdi->supplies);
	if (err < 0) {
		dev_err(panel->dev, "regulator enable failed, %d\n", err);
		return err;
	}
	/*
	 * According to the datasheet, the panel needs around 10 ms to fully
	 * power up. At least another 120 ms is required before exiting sleep
	 * mode to make sure the panel is ready. Throw in another 20 ms for
	 * good measure.
	 */
	msleep(150);

	gpiod_set_value(jdi->disp_lcd_enable, 1);
	usleep_range(10, 20);

	gpiod_set_value(jdi->reset_gpio, 0);
	usleep_range(10, 20);

	gpiod_set_value(jdi->enable_gpio, 1);
	usleep_range(10, 20);

        err = jdi_panel_init(jdi);
	if (err < 0) {
		dev_err(panel->dev, "failed to init panel: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_exit_sleep_mode(jdi->left);
	if (err < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", err);
		goto poweroff;
	}

	/*
	 * The MIPI DCS specification mandates this delay only between the
	 * exit_sleep_mode and enter_sleep_mode commands, so it isn't strictly
	 * necessary here.
	 */
	/*
	msleep(120);
	*/

	/* set left-right mode */
	/*err = jdi_panel_write(jdi, 0x1000, 0x2a);
	if (err < 0) {
		dev_err(panel->dev, "failed to set left-right mode: %d\n", err);
		goto poweroff;
	}*/

	/* enable command mode */
	/*err = jdi_panel_write(jdi, 0x1001, 0x01);
	if (err < 0) {
		dev_err(panel->dev, "failed to enable command mode: %d\n", err);
		goto poweroff;
	}*/

	err = mipi_dsi_dcs_set_pixel_format(jdi->left, format);
	if (err < 0) {
		dev_err(panel->dev, "failed to set pixel format: %d\n", err);
		goto poweroff;
	}

	/*
	 * TODO: The device supports both left-right and even-odd split
	 * configurations, but this driver currently supports only the left-
	 * right split. To support a different mode a mechanism needs to be
	 * put in place to communicate the configuration back to the DSI host
	 * controller.
	 */
	err = jdi_setup_symmetrical_split(jdi->left, jdi->right,
					    jdi->mode);
	if (err < 0) {
		dev_err(panel->dev, "failed to set up symmetrical split: %d\n",
			err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_display_on(jdi->left);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", err);
		goto poweroff;
	}
	
	err = jdi_panel_post_on(jdi);
	if (err < 0) {
		dev_err(jdi->dev, "failed to set panel post on cmds: %d\n", err);
		goto poweroff;
	}
	
	jdi->prepared = true;

	/* wait for 6 frames before continuing */
	jdi_wait_frames(jdi, 6);

	return 0;

poweroff:
	err = regulator_bulk_disable(ARRAY_SIZE(jdi->supplies), jdi->supplies);
	if (err < 0)
		dev_err(jdi->dev, "regulator disable failed, %d\n", err);

	gpiod_set_value(jdi->enable_gpio, 0);

	gpiod_set_value(jdi->reset_gpio, 1);

	gpiod_set_value(jdi->disp_lcd_enable, 0);
	return err;
}

static int jdi_panel_enable(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
        int err;

	if (jdi->enabled)
		return 0;

	if (jdi->backlight) {
		err = backlight_enable(jdi->backlight);
		if (err < 0)
			dev_err(jdi->dev, "backlight enable failed %d\n", err);
	}

	jdi->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = (2560 + 164 + 12 + 80) * (1600 + 12 + 4 + 4) * 60 / 1000,
	//.clock = 278000,
        .hdisplay = 2560,
	.hsync_start = 2560 + 164,
	.hsync_end = 2560 + 164 + 12,
	.htotal = 2560 + 164 + 12 + 80,
	.vdisplay = 1600,
	.vsync_start = 1600 + 12,
	.vsync_end = 1600 + 12 + 4,
	.vtotal = 1600 + 12 + 4 + 4,
};

static int jdi_panel_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 193;
	connector->display_info.height_mm = 122;

	return 1;
}

static const struct drm_panel_funcs jdi_panel_funcs = {
	.disable = jdi_panel_disable,
	.unprepare = jdi_panel_unprepare,
	.prepare = jdi_panel_prepare,
	.enable = jdi_panel_enable,
	.get_modes = jdi_panel_get_modes,
};

static const struct of_device_id jdi_of_match[] = {
	{ .compatible = "jdi,qhd-apollo", },
	{ }
};
MODULE_DEVICE_TABLE(of, jdi_of_match);

static int jdi_panel_add(struct jdi_panel *jdi)
{
	int ret;
	unsigned int i;

	jdi->mode = &default_mode;

	for (i = 0; i < ARRAY_SIZE(jdi->supplies); i++)
		jdi->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(jdi->dev, ARRAY_SIZE(jdi->supplies),
				      jdi->supplies);
	if (ret < 0) {
		dev_err(jdi->dev, "failed to init regulator, ret=%d\n", ret);
		return ret;
	}

	jdi->enable_gpio = devm_gpiod_get(jdi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(jdi->enable_gpio)) {
		ret = PTR_ERR(jdi->enable_gpio);
		dev_err(jdi->dev, "cannot get enable-gpio %d\n", ret);
		return ret;
	}

	jdi->reset_gpio = devm_gpiod_get(jdi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(jdi->reset_gpio)) {
		ret = PTR_ERR(jdi->reset_gpio);
		dev_err(jdi->dev, "cannot get reset-gpios %d\n", ret);
		return ret;
	}

	jdi->disp_lcd_enable = devm_gpiod_get(jdi->dev, "lcdenable", GPIOD_OUT_HIGH);
	if (IS_ERR(jdi->disp_lcd_enable)) {
		ret = PTR_ERR(jdi->disp_lcd_enable);
		dev_err(jdi->dev, "cannot get lcd power enable-gpio %d\n", ret);
		return ret;
	}

	jdi->lcd_mbist = devm_gpiod_get(jdi->dev, "lcdmbist", GPIOD_OUT_HIGH);
	if (IS_ERR(jdi->lcd_mbist)) {
		dev_err(jdi->dev, "cannot get mbist-gpio %d\n", ret);
		return ret;
	}

	drm_panel_init(&jdi->base, &jdi->left->dev, &jdi_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&jdi->base);
	if (ret)
		return ret;

	drm_panel_add(&jdi->base);

	return 0;
}

static void jdi_panel_del(struct jdi_panel *jdi)
{
	if (jdi->base.dev)
		drm_panel_remove(&jdi->base);

	if (jdi->right)
		put_device(&jdi->right->dev);
}

static int jdi_panel_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_device *secondary = NULL;
	struct jdi_panel *jdi;
	struct device_node *np;
	int err;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM;

	/* Find DSI-left */
	np = of_parse_phandle(dsi->dev.of_node, "right", 0);
	if (np) {
		secondary = of_find_mipi_dsi_device_by_node(np);
		of_node_put(np);

		if (!secondary)
			return -EPROBE_DEFER;
	}

	/* register a panel for only the DSI-left interface */
	if (secondary) {
		jdi = devm_kzalloc(&dsi->dev, sizeof(*jdi), GFP_KERNEL);
		if (!jdi) {
			put_device(&secondary->dev);
			return -ENOMEM;
		}

		mipi_dsi_set_drvdata(dsi, jdi);

		jdi->right = secondary;
		jdi->left = dsi;

		err = jdi_panel_add(jdi);
		if (err < 0) {
			put_device(&secondary->dev);
			return err;
		}
	}

	err = mipi_dsi_attach(dsi);
	if (err < 0) {
		if (secondary)
			jdi_panel_del(jdi);

		return err;
	}

	return 0;
}

static int jdi_panel_remove(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi = mipi_dsi_get_drvdata(dsi);
	int err;

	/* only detach from host for the DSI-right interface */
	if (!jdi) {
		mipi_dsi_detach(dsi);
		return 0;
	}

	err = drm_panel_disable(&jdi->base);
	if (err < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	jdi_panel_del(jdi);

	return 0;
}

static void jdi_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi = mipi_dsi_get_drvdata(dsi);

	/* nothing to do for DSI-right */
	if (!jdi)
		return;

	drm_panel_disable(&jdi->base);
}

static struct mipi_dsi_driver jdi_panel_driver = {
	.driver = {
		.name = "panel-jdi-qhd",
		.of_match_table = jdi_of_match,
	},
	.probe = jdi_panel_probe,
	.remove = jdi_panel_remove,
	.shutdown = jdi_panel_shutdown,
};
module_mipi_dsi_driver(jdi_panel_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("JDI QHD Dual Mipi");
MODULE_LICENSE("GPL v2");

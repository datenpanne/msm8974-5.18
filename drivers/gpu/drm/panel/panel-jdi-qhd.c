// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 InforceComputing
 * Author: Vinay Simha BN <simhavcs@gmail.com>
 *
 * Copyright (C) 2016 Linaro Ltd
 * Author: Sumit Semwal <sumit.semwal@linaro.org>
 *
 */
 
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

static const char * const regulator_names[] = {
	"vddp",
	"iovcc"
};

struct jdi_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi[2];

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

static int jdi_panel_init(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi[0];
	struct device *dev = &jdi->dsi->dev;
	int ret;

	dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	dsi[1]->mode_flags |= MIPI_DSI_MODE_LPM;
	
	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0)
		return ret;

	usleep_range(10000, 20000);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, MIPI_DCS_PIXEL_FMT_24BIT << 4);
	if (ret < 0) {
		dev_err(dev, "failed to set pixel format: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_column_address(dsi, 0, jdi->mode->hdisplay - 1);
	if (ret < 0) {
		dev_err(dev, "failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, jdi->mode->vdisplay - 1);
	if (ret < 0) {
		dev_err(dev, "failed to set page address: %d\n", ret);
		return ret;
	}

	/*
	 * BIT(5) BCTRL = 1 Backlight Control Block On, Brightness registers
	 *                  are active
	 * BIT(3) BL = 1    Backlight Control On
	 * BIT(2) DD = 0    Display Dimming is Off
	 */
	/*ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				 (u8[]){ 0x24 }, 1);
	if (ret < 0) {
		dev_err(dev, "failed to write control display: %d\n", ret);
		return ret;
	}*/

	/* CABC off */
	/*ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_POWER_SAVE,
				 (u8[]){ 0x00 }, 1);
	if (ret < 0) {
		dev_err(dev, "failed to set cabc off: %d\n", ret);
		return ret;
	}*/

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_tear_scanline(dsi, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear scanline: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to set exit sleep mode: %d\n", ret);
		return ret;
	}

	msleep(120);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x00}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set mcap: %d\n", ret);
		return ret;
	}

	mdelay(10);

	/* Interface setting */
	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB3, 0x14}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set display interface setting: %d\n"
			, ret);
		return ret;
	}

	mdelay(20);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x03}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set default values for mcap: %d\n"
			, ret);
		return ret;
	}

	return 0;
}

static int jdi_panel_on(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi[0];
	struct device *dev = &jdi->dsi->dev;
	int ret;

	dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	dsi[1]->mode_flags |= MIPI_DSI_MODE_LPM;
	
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		dev_err(dev, "failed to set display on: %d\n", ret);

	return ret;
}

static int jdi_panel_post_on(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi[0];
	struct device *dev = &jdi->dsi->dev;
	int ret;
	
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM; //hs
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM; //hs
	
	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x00}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set mcap: %d\n", ret);
		return ret;
	}

	mdelay(10);

	/* Interface setting */
	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB3, 0xc3}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set display interface setting: %d\n"
			, ret);
		return ret;
	}

	mdelay(20);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x03}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set default values for mcap: %d\n"
			, ret);
		return ret;
	}
	return 0;	
}

static void jdi_panel_off(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi[0];
	struct device *dev = &jdi->dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(dev, "failed to enter sleep mode: %d\n", ret);

	msleep(100);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x00}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set mcap: %d\n", ret);
		return ret;
	}

	mdelay(10);

	/* Interface setting */
	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB3, 0xc1}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set display interface setting: %d\n"
			, ret);
		return ret;
	}

	mdelay(20);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x03}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set default values for mcap: %d\n"
			, ret);
		return ret;
	}

}

static int jdi_panel_disable(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	int ret;

	if (!jdi->enabled)
		return 0;
		
	if (jdi->backlight) {
		ret = backlight_disable(jdi->backlight);
		if (ret < 0)
			dev_err(jdi->dev, "backlight disable failed %d\n", ret);
	}
	
	jdi->enabled = false;

	return 0;
}*

static int jdi_panel_unprepare(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	struct device *dev = &jdi->dsi[0]->dev;
	int ret;

	if (!jdi->prepared)
		return 0;

	jdi_panel_off(jdi);

	ret = regulator_bulk_disable(ARRAY_SIZE(jdi->supplies), jdi->supplies);
	if (ret < 0)
		dev_err(dev, "regulator disable failed, %d\n", ret);

	gpiod_set_value(jdi->enable_gpio, 0);

	gpiod_set_value(jdi->reset_gpio, 1);

	gpiod_set_value(jdi->disp_lcd_enable, 0);

	jdi->prepared = false;

	return 0;
}

static int jdi_panel_prepare(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	struct device *dev = &jdi->dsi[0]->dev;
	int ret;

	if (jdi->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(jdi->supplies), jdi->supplies);
	if (ret < 0) {
		dev_err(dev, "regulator enable failed, %d\n", ret);
		return ret;
	}

	msleep(20);

	gpiod_set_value(jdi->disp_lcd_enable, 1);
	usleep_range(10, 20);

	gpiod_set_value(jdi->reset_gpio, 0);
	usleep_range(10, 20);

	gpiod_set_value(jdi->enable_gpio, 1);
	usleep_range(10, 20);

	ret = jdi_panel_init(jdi);
	if (ret < 0) {
		dev_err(dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	ret = jdi_panel_on(jdi);
	if (ret < 0) {
		dev_err(dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}
	
	ret = jdi_panel_post_on(jdi);
	if (ret < 0) {
		dev_err(dev, "failed to set panel post on cmds: %d\n", ret);
		goto poweroff;
	}

	jdi->prepared = true;

	return 0;

poweroff:
	ret = regulator_bulk_disable(ARRAY_SIZE(jdi->supplies), jdi->supplies);
	if (ret < 0)
		dev_err(dev, "regulator disable failed, %d\n", ret);

	gpiod_set_value(jdi->enable_gpio, 0);

	gpiod_set_value(jdi->reset_gpio, 1);

	gpiod_set_value(jdi->disp_lcd_enable, 0);

	return ret;
}

static int jdi_panel_enable(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);

	if (jdi->enabled)
		return 0;

	if (jdi->backlight) {
		ret = backlight_enable(jdi->backlight);
		if (ret < 0)
			dev_err(jdi->dev, "backlight disable failed %d\n", ret);
	}

	jdi->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
		.clock = 155493,
		.hdisplay = 1600,
		.hsync_start = 1600 + 164,
		.hsync_end = 1600 + 164 + 12,
		.htotal = 1600 + 164 + 12 + 80,
		.vdisplay = 2560,
		.vsync_start = 2560 + 12,
		.vsync_end = 2560 + 12 + 4,
		.vtotal = 2560 + 12 + 4 + 4,
		.flags = 0,
};

static int jdi_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct jdi_panel *jdi = to_jdi_panel(panel);
	struct device *dev = &jdi->dsi->dev;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 95;
	connector->display_info.height_mm = 151;

	return 1;
}

/*static int dsi_dcs_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;
	u16 brightness = bl->props.brightness;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static int dsi_dcs_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops dsi_bl_ops = {
	.update_status = dsi_dcs_bl_update_status,
	.get_brightness = dsi_dcs_bl_get_brightness,
};

static struct backlight_device *
drm_panel_create_dsi_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.brightness = 255;
	props.max_brightness = 255;

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &dsi_bl_ops, &props);
}*/

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
	struct device *dev = &jdi->dsi[0]->dev;
	int ret;
	unsigned int i;

	jdi->mode = &default_mode;

	for (i = 0; i < ARRAY_SIZE(jdi->supplies); i++)
		jdi->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(jdi->supplies),
				      jdi->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to init regulator, ret=%d\n", ret);
		return ret;
	}

	jdi->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(jdi->enable_gpio)) {
		ret = PTR_ERR(jdi->enable_gpio);
		dev_err(dev, "cannot get enable-gpio %d\n", ret);
		return ret;
	}

	jdi->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(jdi->reset_gpio)) {
		ret = PTR_ERR(jdi->reset_gpio);
		dev_err(dev, "cannot get reset-gpios %d\n", ret);
		return ret;
	}

	jdi->disp_lcd_enable = devm_gpiod_get(dev, "lcdenable", GPIOD_OUT_HIGH);
	if (IS_ERR(jdi->disp_lcd_enable)) {
		ret = PTR_ERR(jdi->disp_lcd_enable);
		dev_err(dev, "cannot get lcd power enable-gpio %d\n", ret);
		return ret;
	}

	jdi->lcd_mbist = devm_gpiod_get(dev, "lcdmbist", GPIOD_OUT_HIGH);
	if (IS_ERR(jdi->lcd_mbist)) {
		dev_err(dev, "cannot get mbist-gpio %d\n", ret);
		return ret;
	}

	/*jdi->backlight = drm_panel_create_dsi_backlight(jdi->dsi);
	if (IS_ERR(jdi->backlight)) {
		ret = PTR_ERR(jdi->backlight);
		dev_err(dev, "failed to register backlight %d\n", ret);
		return ret;
	}*/

	drm_panel_init(&jdi->base, &jdi->dsi->dev, &jdi_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
		       
	ret = drm_panel_of_backlight(&jdi->base);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");
		
	drm_panel_add(&jdi->base);

	return 0;
}

/*static void jdi_panel_del(struct jdi_panel *jdi)
{
	if (jdi->base.dev)
		drm_panel_remove(&jdi->base);
}*/

static int jdi_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jdi_panel *jdi;
	struct mipi_dsi_device *dsi_r_device;
	struct device_node *dsi_r;
	struct mipi_dsi_host *dsi_r_host;
	struct mipi_dsi_device *dsi_r_device;
	int ret = 0;
	int i;
	
	const struct mipi_dsi_device_info info = {
		.type = "jdiqhd",
		.channel = 0,
		.node = NULL,
	};

	jdi = devm_kzalloc(&dsi->dev, sizeof(*jdi), GFP_KERNEL);
	if (!jdi)
		return -ENOMEM;

	dsi_r = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (!dsi_r) {
		dev_err(dev, "failed to get remote node for dsi_r_device\n");
		return -ENODEV;
	}

	dsi_r_host = of_find_mipi_dsi_host_by_node(dsi_r);
	of_node_put(dsi_r);
	if (!dsi_r_host) {
		dev_err(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	/* register the second DSI device */
	dsi_r_device = mipi_dsi_device_register_full(dsi_r_host, &info);
	if (IS_ERR(dsi_r_device)) {
		dev_err(dev, "failed to create dsi device\n");
		return PTR_ERR(dsi_r_device);
	}

	mipi_dsi_set_drvdata(dsi, jdi);
	
	ctx->dev = dev;
	ctx->dsi[0] = dsi;
	ctx->dsi[1] = dsi_r_device;

	ret = jdi_panel_add(jdi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		jdi_panel_del(jdi);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		dsi_dev = ctx->dsi[i];
		dsi_dev->lanes = 4;
		dsi_dev->format = MIPI_DSI_FMT_RGB888;
		dsi_dev->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM |
			MIPI_DSI_CLOCK_NON_CONTINUOUS;
		ret = mipi_dsi_attach(dsi_dev);
		if (ret < 0) {
			dev_err(dev, "dsi attach failed i = %d\n", i);
			goto err_dsi_attach;
		}
	}

	return 0;
}

static int jdi_panel_remove(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi = mipi_dsi_get_drvdata(dsi);
	int ret;

	/*ret = jdi_panel_disable(&jdi->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);*/

	if (ctx->dsi[0])
		mipi_dsi_detach(dsi[0]);
	if (ctx->dsi[1]) {
		mipi_dsi_detach(dsi[1]);
		mipi_dsi_device_unregister(dsi[1]);
	}
	
	//jdi_panel_del(jdi);
	drm_panel_remove(jdi);
	return 0;
}

/*static void jdi_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi = mipi_dsi_get_drvdata(dsi);

	jdi_panel_disable(&jdi->base);
}*/

static struct mipi_dsi_driver jdi_panel_driver = {
	.driver = {
		.name = "panel-jdi-qhd",
		.of_match_table = jdi_of_match,
	},
	.probe = jdi_panel_probe,
	.remove = jdi_panel_remove,
	//.shutdown = jdi_panel_shutdown,
};
module_mipi_dsi_driver(jdi_panel_driver);

MODULE_AUTHOR("Sumit Semwal <sumit.semwal@linaro.org>");
MODULE_AUTHOR("Vinay Simha BN <simhavcs@gmail.com>");
MODULE_DESCRIPTION("JDI QHD Dual Mipi");
MODULE_LICENSE("GPL v2");

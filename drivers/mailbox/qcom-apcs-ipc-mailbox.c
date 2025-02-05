// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, Linaro Ltd
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mailbox_controller.h>
#include <linux/ipc_logging.h>

#define QCOM_APCS_IPC_BITS	32
#define IPC_LOG_PAGE_CNT    64

static void *apcs_ipc_log;

#define APCS_INFO(ctx, x, ...) \
    ipc_log_string(ctx, x, ##__VA_ARGS__)

struct qcom_apcs_ipc {
	struct mbox_controller mbox;
	struct mbox_chan mbox_chans[QCOM_APCS_IPC_BITS];

	struct regmap *regmap;
	unsigned long offset;
	struct platform_device *clk;
};

struct qcom_apcs_ipc_data {
	int offset;
	char *clk_name;
};

static const struct qcom_apcs_ipc_data ipq6018_apcs_data = {
	.offset = 8, .clk_name = "qcom,apss-ipq6018-clk"
};

static const struct qcom_apcs_ipc_data ipq8074_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data msm8916_apcs_data = {
	.offset = 8, .clk_name = "qcom-apcs-msm8916-clk"
};

static const struct qcom_apcs_ipc_data msm8994_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data msm8996_apcs_data = {
	.offset = 16, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data msm8998_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data sdm660_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data sm6125_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data apps_shared_apcs_data = {
	.offset = 12, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data sdx55_apcs_data = {
	.offset = 0x1008, .clk_name = "qcom-sdx55-acps-clk"
};

static const struct qcom_apcs_ipc_data bengal_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data trinket_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data scuba_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct qcom_apcs_ipc_data monaco_apcs_data = {
	.offset = 8, .clk_name = NULL
};

static const struct regmap_config apcs_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1008,
	.fast_io = true,
};

static int qcom_apcs_ipc_send_data(struct mbox_chan *chan, void *data)
{
	int ret;
	struct qcom_apcs_ipc *apcs = container_of(chan->mbox,
						  struct qcom_apcs_ipc, mbox);
	unsigned long idx = (unsigned long)chan->con_priv;

    ret = regmap_write(apcs->regmap, apcs->offset, BIT(idx));
    APCS_INFO(apcs_ipc_log, "[syh]write[offset:0x%lx val:0x%x], ret:%d\n", apcs->offset, BIT(idx), ret);

    return ret;
}

static const struct mbox_chan_ops qcom_apcs_ipc_ops = {
	.send_data = qcom_apcs_ipc_send_data,
};

static int qcom_apcs_ipc_probe(struct platform_device *pdev)
{
	struct qcom_apcs_ipc *apcs;
	const struct qcom_apcs_ipc_data *apcs_data;
	struct regmap *regmap;
	struct resource *res;
	void __iomem *base;
	unsigned long i;
	int ret;

	apcs = devm_kzalloc(&pdev->dev, sizeof(*apcs), GFP_KERNEL);
	if (!apcs)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base, &apcs_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	apcs_data = of_device_get_match_data(&pdev->dev);
	if (!apcs_data)
		return -EINVAL;

	apcs->regmap = regmap;
	apcs->offset = apcs_data->offset;

	/* Initialize channel identifiers */
	for (i = 0; i < ARRAY_SIZE(apcs->mbox_chans); i++)
		apcs->mbox_chans[i].con_priv = (void *)i;

	apcs->mbox.dev = &pdev->dev;
	apcs->mbox.ops = &qcom_apcs_ipc_ops;
	apcs->mbox.chans = apcs->mbox_chans;
	apcs->mbox.num_chans = ARRAY_SIZE(apcs->mbox_chans);

	ret = devm_mbox_controller_register(&pdev->dev, &apcs->mbox);
	if (ret) {
		dev_err(&pdev->dev, "failed to register APCS IPC controller\n");
		return ret;
	}

	if (apcs_data->clk_name) {
		apcs->clk = platform_device_register_data(&pdev->dev,
							  apcs_data->clk_name,
							  PLATFORM_DEVID_AUTO,
							  NULL, 0);
		if (IS_ERR(apcs->clk))
			dev_err(&pdev->dev, "failed to register APCS clk\n");
	}

	platform_set_drvdata(pdev, apcs);
	
	apcs_ipc_log = ipc_log_context_create(IPC_LOG_PAGE_CNT, "apcs", 0);

	return 0;
}

static int qcom_apcs_ipc_remove(struct platform_device *pdev)
{
	struct qcom_apcs_ipc *apcs = platform_get_drvdata(pdev);
	struct platform_device *clk = apcs->clk;

	platform_device_unregister(clk);

	return 0;
}

/* .data is the offset of the ipc register within the global block */
static const struct of_device_id qcom_apcs_ipc_of_match[] = {
	{ .compatible = "qcom,ipq6018-apcs-apps-global", .data = &ipq6018_apcs_data },
	{ .compatible = "qcom,ipq8074-apcs-apps-global", .data = &ipq8074_apcs_data },
	{ .compatible = "qcom,msm8916-apcs-kpss-global", .data = &msm8916_apcs_data },
	{ .compatible = "qcom,msm8939-apcs-kpss-global", .data = &msm8916_apcs_data },
	{ .compatible = "qcom,msm8953-apcs-kpss-global", .data = &msm8994_apcs_data },
	{ .compatible = "qcom,msm8994-apcs-kpss-global", .data = &msm8994_apcs_data },
	{ .compatible = "qcom,msm8996-apcs-hmss-global", .data = &msm8996_apcs_data },
	{ .compatible = "qcom,msm8998-apcs-hmss-global", .data = &msm8998_apcs_data },
	{ .compatible = "qcom,qcs404-apcs-apps-global", .data = &msm8916_apcs_data },
	{ .compatible = "qcom,monaco-apcs-hmss-global", .data = &monaco_apcs_data  },
	{ .compatible = "qcom,sc7180-apss-shared", .data = &apps_shared_apcs_data },
	{ .compatible = "qcom,sc8180x-apss-shared", .data = &apps_shared_apcs_data },
	{ .compatible = "qcom,sdm660-apcs-hmss-global", .data = &sdm660_apcs_data },
	{ .compatible = "qcom,sdm845-apss-shared", .data = &apps_shared_apcs_data },
	{ .compatible = "qcom,sm6125-apcs-hmss-global", .data = &sm6125_apcs_data },
	{ .compatible = "qcom,sm8150-apss-shared", .data = &apps_shared_apcs_data },
	{ .compatible = "qcom,sm6115-apcs-hmss-global", .data = &sdm660_apcs_data },
	{ .compatible = "qcom,sdx55-apcs-gcc", .data = &sdx55_apcs_data },
	{ .compatible = "qcom,bengal-apcs-hmss-global", .data = &bengal_apcs_data },
	{ .compatible = "qcom,scuba-apcs-hmss-global", .data = &scuba_apcs_data },
	{ .compatible = "qcom,trinket-apcs-hmss-global", .data = &trinket_apcs_data },
	{ .compatible = "qcom,qcs605-apcs-shared", .data = &apps_shared_apcs_data },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_apcs_ipc_of_match);

static struct platform_driver qcom_apcs_ipc_driver = {
	.probe = qcom_apcs_ipc_probe,
	.remove = qcom_apcs_ipc_remove,
	.driver = {
		.name = "qcom_apcs_ipc",
		.of_match_table = qcom_apcs_ipc_of_match,
	},
};

static int __init qcom_apcs_ipc_init(void)
{
	return platform_driver_register(&qcom_apcs_ipc_driver);
}
postcore_initcall(qcom_apcs_ipc_init);

static void __exit qcom_apcs_ipc_exit(void)
{
	platform_driver_unregister(&qcom_apcs_ipc_driver);
}
module_exit(qcom_apcs_ipc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm APCS IPC driver");

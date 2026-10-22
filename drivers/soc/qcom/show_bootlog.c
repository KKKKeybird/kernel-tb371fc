// SPDX-License-Identifier: GPL-2.0
/*
 * show_bootlog - Qualcomm bootlog diagnostic driver
 * D263: R5 gap fill - stock Image-b has this driver (0x8880498-0x88807d8)
 * compatible "debug,bootlog", reads boot_log_region@0 reserved memory
 * (0xb0000000, 512KB) and exposes it via procfs.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

static void __iomem *bootlog_vaddr;
static u32 bootlog_size;
static bool first_read_done;

static int bootlog_proc_show(struct seq_file *m, void *v)
{
	u32 i;

	if (!bootlog_vaddr)
		return 0;

	for (i = 0; i < bootlog_size; i += 64) {
		u32 len = min(64u, bootlog_size - i);
		char buf[65];
		u32 j;

		for (j = 0; j < len; j++)
			buf[j] = readb(bootlog_vaddr + i + j);
		buf[len] = 0;
		seq_puts(m, buf);
	}
	if (!first_read_done) {
		first_read_done = true;
		pr_info("bootlog: The First bootlog print complete\n");
		pr_info("bootlog: The second bootlog print complete\n");
	}
	return 0;
}

static int bootlog_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootlog_proc_show, NULL);
}

static const struct file_operations bootlog_proc_fops = {
	.open = bootlog_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int show_bootlog_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct resource r;
	int ret;

	dev_info(&pdev->dev, "bootlog: enter %s\n", __func__);

	/* Stock parses the chosen node bootlog_mem path, but our DT does
	 * not carry that vendor property.  Fall back to a direct path
	 * lookup for the reserved memory node (confirmed present at
	 * /reserved-memory/boot_log_region@0 in the running DTB). */
	np = of_find_node_by_path("/reserved-memory/boot_log_region@0");
	if (!np) {
		struct device_node *chosen = of_find_node_by_path("/chosen");
		const char *path = NULL;

		if (chosen) {
			of_property_read_string(chosen, "bootlog_mem", &path);
			of_node_put(chosen);
		}
		if (path)
			np = of_find_node_by_path(path);
	}
	if (!np) {
		dev_err(&pdev->dev,
			"bootlog: mem reservation for bootlog not find\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (ret) {
		dev_err(&pdev->dev,
			"bootlog: failed to parse the bootlog reserve memory\n");
		return ret;
	}

	bootlog_size = (u32)resource_size(&r);
	dev_info(&pdev->dev, "bootlog: phys_base=0x%x, size=0x%x\n",
		 (u32)r.start, bootlog_size);

	bootlog_vaddr = devm_ioremap(&pdev->dev, r.start, bootlog_size);
	if (!bootlog_vaddr) {
		dev_err(&pdev->dev, "bootlog: Error allocating memory\n");
		return -ENOMEM;
	}

	dev_info(&pdev->dev, "bootlog: the log_size is 0x%x\n", bootlog_size);
	proc_create("bootlog", 0444, NULL, &bootlog_proc_fops);
	dev_info(&pdev->dev, "bootlog: current address %p\n", bootlog_vaddr);

	return 0;
}

static int show_bootlog_remove(struct platform_device *pdev)
{
	remove_proc_entry("bootlog", NULL);
	bootlog_vaddr = NULL;
	return 0;
}

static const struct of_device_id bootlog_of_match[] = {
	{ .compatible = "debug,bootlog" },
	{ }
};
MODULE_DEVICE_TABLE(of, bootlog_of_match);

static struct platform_driver show_bootlog_driver = {
	.probe = show_bootlog_probe,
	.remove = show_bootlog_remove,
	.driver = {
		.name = "show_bootlog",
		.of_match_table = bootlog_of_match,
	},
};

static struct platform_device *bootlog_pdev;

static int __init show_bootlog_init(void)
{
	int ret;

	ret = platform_driver_register(&show_bootlog_driver);
	if (ret)
		return ret;

	/* Stock creates the platform device manually since the DT does not
	 * have a platform node with compatible "debug,bootlog" - only the
	 * reserved-memory region exists. */
	bootlog_pdev = platform_device_register_simple("show_bootlog",
			-1, NULL, 0);
	if (IS_ERR(bootlog_pdev)) {
		platform_driver_unregister(&show_bootlog_driver);
		return PTR_ERR(bootlog_pdev);
	}

	return 0;
}
module_init(show_bootlog_init);

static void __exit show_bootlog_exit(void)
{
	platform_device_unregister(bootlog_pdev);
	platform_driver_unregister(&show_bootlog_driver);
}
module_exit(show_bootlog_exit);

MODULE_DESCRIPTION("Qualcomm bootlog diagnostic driver");
MODULE_LICENSE("GPL v2");

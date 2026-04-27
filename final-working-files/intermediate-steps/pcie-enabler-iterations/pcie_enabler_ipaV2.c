#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/byteorder/generic.h>

#define PCIE_COMPAT_1 "qcom,pcie-sdx65"
#define PCIE_COMPAT_2 "qcom,pcie-sdx62"
#define PCIE_COMPAT_3 "snps,dw-pcie"

static struct device_node *pcie_node = NULL;
static struct platform_device *pcie_pdev = NULL;

static int __init pcie_ipa_enabler_init(void)
{
    struct property *pp;
    int status_found = 0;
    
    // Variabili per l'IOMMU
    struct property *iommu_prop;
    __be32 *iommu_map_data;

    // Variabili per il nodo Ethernet
    struct device_node *eth_node;
    struct property *compat_prop, *rx_ring_prop, *tx_ring_prop;
    __be32 *rx_ring_data;
    __be32 *tx_ring_data;

    printk(KERN_INFO "[PCIe Hacker] Avvio iniezione strutturale RAM...\n");

    // 1. Trova il nodo Radice PCIe
    pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_1);
    if (!pcie_node) pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_2);
    if (!pcie_node) pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_3);

    if (!pcie_node) {
        printk(KERN_ERR "[PCIe Hacker] ERRORE: Nodo Root Complex non trovato!\n");
        return -ENODEV;
    }

    // 2. Manipolazione brutale dello Status (Il trucco gi? collaudato)
    for_each_property_of_node(pcie_node, pp) {
        if (strcmp(pp->name, "status") == 0) {
            pp->value = "okay";
            pp->length = 5;
            status_found = 1;
            printk(KERN_INFO "[PCIe Hacker] Status forzato a 'okay'.\n");
            break;
        }
    }
    if (!status_found) printk(KERN_WARNING "[PCIe Hacker] Warning: 'status' non trovato.\n");

    // 3. Iniezione dell'IOMMU-MAP (Sblocco DMA verso l'IPA)
    // Array: <RID_base SMMU_phandle SMR_ID Length>
    iommu_map_data = kmalloc_array(4, sizeof(__be32), GFP_KERNEL);
    if (iommu_map_data) {
        iommu_map_data[0] = cpu_to_be32(0x0);      // RID base
        iommu_map_data[1] = cpu_to_be32(0x34);     // Il Phandle SMMU esatto del tuo T99!
        iommu_map_data[2] = cpu_to_be32(0x1400);   // Stream ID tipico dell'IPA su SDX62
        iommu_map_data[3] = cpu_to_be32(0x1);      // Length

        iommu_prop = kzalloc(sizeof(*iommu_prop), GFP_KERNEL);
        if (iommu_prop) {
            iommu_prop->name = kstrdup("iommu-map", GFP_KERNEL);
            iommu_prop->value = iommu_map_data;
            iommu_prop->length = 4 * sizeof(__be32);

            // Prependi in cima alla lista delle propriet? (Bypass API)
            iommu_prop->next = pcie_node->properties;
            pcie_node->properties = iommu_prop;
            printk(KERN_INFO "[PCIe Hacker] Proprieta' 'iommu-map' agganciata (SMMU 0x34).\n");
        }
    }

    // 4. Costruzione del Sotto-Nodo Ethernet (Per il driver Quectel r8125)
    eth_node = kzalloc(sizeof(*eth_node), GFP_KERNEL);
    if (eth_node) {
        // Pseudo-inizializzazione del nodo
        kref_init(&eth_node->kobj.kref);
        eth_node->name = kstrdup("ethernet", GFP_KERNEL);
        eth_node->full_name = kstrdup("ethernet@0,0", GFP_KERNEL);

        // A. Propriet?: compatible
        compat_prop = kzalloc(sizeof(*compat_prop), GFP_KERNEL);
        compat_prop->name = kstrdup("compatible", GFP_KERNEL);
        compat_prop->value = kstrdup("realtek,r8125\0ethernet", GFP_KERNEL);
        compat_prop->length = 23;
        compat_prop->next = eth_node->properties;
        eth_node->properties = compat_prop;

        // B. Propriet?: qcom,ipa-rx-ring-size
        rx_ring_data = kmalloc(sizeof(__be32), GFP_KERNEL);
        *rx_ring_data = cpu_to_be32(0x1000);
        rx_ring_prop = kzalloc(sizeof(*rx_ring_prop), GFP_KERNEL);
        rx_ring_prop->name = kstrdup("qcom,ipa-rx-ring-size", GFP_KERNEL);
        rx_ring_prop->value = rx_ring_data;
        rx_ring_prop->length = sizeof(__be32);
        rx_ring_prop->next = eth_node->properties;
        eth_node->properties = rx_ring_prop;

        // C. Propriet?: qcom,ipa-tx-ring-size
        tx_ring_data = kmalloc(sizeof(__be32), GFP_KERNEL);
        *tx_ring_data = cpu_to_be32(0x1000);
        tx_ring_prop = kzalloc(sizeof(*tx_ring_prop), GFP_KERNEL);
        tx_ring_prop->name = kstrdup("qcom,ipa-tx-ring-size", GFP_KERNEL);
        tx_ring_prop->value = tx_ring_data;
        tx_ring_prop->length = sizeof(__be32);
        tx_ring_prop->next = eth_node->properties;
        eth_node->properties = tx_ring_prop;

        // Aggancio il nodo Ethernet come figlio del nodo PCIe
        eth_node->parent = pcie_node;
        eth_node->sibling = pcie_node->child;
        pcie_node->child = eth_node;

        printk(KERN_INFO "[PCIe Hacker] Sotto-nodo 'ethernet@0,0' clonato e innestato.\n");
    }

    // 5. Sveglia il colosso
    pcie_pdev = of_platform_device_create(pcie_node, NULL, NULL);
    if (!pcie_pdev) {
        printk(KERN_ERR "[PCIe Hacker] ERRORE: of_platform_device_create fallito.\n");
        return -ENODEV;
    }

    printk(KERN_INFO "[PCIe Hacker] SUCCESSO! Platform device PCIe ricreato con albero patchato.\n");
    return 0; 
}

static void __exit pcie_ipa_enabler_exit(void)
{
    printk(KERN_INFO "[PCIe Hacker] Rimozione modulo...\n");
    if (pcie_pdev) of_device_unregister(pcie_pdev);
    // Non liberiamo volontariamente la memoria allocata (leak intenzionale) 
    // per evitare Kernel Panic se altri subsystem hanno preso in carico i nodi.
}

module_init(pcie_ipa_enabler_init);
module_exit(pcie_ipa_enabler_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LTE Hacker");
MODULE_DESCRIPTION("Inietta IOMMU e Code IPA nel DTB in RAM");
/*
 * File: pci_names.h
 * This file was generated automatically. Don't modify it.
*/
#ifndef PCI_NAMES_INCLUDED
#define PCI_NAMES_INCLUDED 1

#ifdef __cplusplus
extern "C" {
#endif

struct device_id_s
{
	unsigned short	id;
	const char *	name;
};

struct vendor_id_s
{
	unsigned short	id;
	const char *	name;
	const struct device_id_s *	dev_list;
};
extern const char *pci_vendor_name(unsigned short id);
extern const char *pci_device_name(unsigned short vendor_id, unsigned short device_id);

#ifdef __cplusplus
}
#endif

#endif/*PCI_NAMES_INCLUDED*/


# -----------------------------------------------------------------|
# sercomm add
# -----------------------------------------------------------------|
EXT_CPU_ARCH_NAME=arma9
export EXT_CPU_ARCH_NAME

# Setup sc build options.
ifeq ($(SC_BUILD),1)
BCM_DIR = $(BUILD_DIR)/bin
LIBDIR = $(SC_ROOT)/tools/mips-linux-uclibc/lib
SC_LIB_PATH = -L$(SC_ROOT)/target/lib -L$(BCM_DIR)
FS_DIR = $(SC_ROOT)/target
export BCM_DIR LIBDIR SC_LIB_PATH
export BUILD_WLCTL_SHLIB=1
export SC_BUILD=1
endif

help:
	@echo "make help      --> show usage, sercomm modified"
	@echo "make sc_kernel --> build vmlinux.lz"
	@echo "make clean     --> clean kernel"
	@echo "make sc_clean     --> clean kernel"
	@echo "make sc_driver --> install drivers"      
	@echo "make sc_GPL    --> GPL release"  
	@echo "make bcm_fw    --> build bcm code"  
	@echo "make bcm_clean --> clean bcm code"  
	@echo "make note      --> driver change note"

GPL_DIR=/root/fw/GPL

note:
	@cat README

sc_clean: bcmdrivers_clean kernel_clean xchange_clean wlan_clean nvram_3k_kernelclean target_clean
	rm -f $(HOSTTOOLS_DIR)/scripts/lxdialog/*.o
	rm -f .tmpconfig*
	rm -f $(LAST_PROFILE_COOKIE)
	
sc_GPL:
	@echo
	@echo "<-- GPL for bcm kernel -->"
	@echo " GPL_DIR = $(GPL_DIR) "
#-- copy files --
	make clean
	cp -ra ./  $(GPL_DIR)/kernel
#-- remove files -- 
	rm -rf $(GPL_DIR)/kernel/bin
	rm -rf $(GPL_DIR)/kernel/userapps
	rm -f $(GPL_DIR)/kernel/shared
	cp -ra ../../Bootcode/bcm963xx_cfe_src_010037_1021/shared $(GPL_DIR)/kernel/
#-- remove not open bcm drivers --
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/atm
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/atmbondingeth
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/net
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/atmapi        
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/atmapi        
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/atmbonding    
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/bcmprocfs     
	rm -rf $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/profiler      
#-- copy needed headers --
	@mkdir -p $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/softdsl
	@cp bcmdrivers/broadcom/char/adsl/impl1/AdslCoreMap.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/
	@cp bcmdrivers/broadcom/char/adsl/impl1/softdsl/AdslCoreDefs.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/softdsl/
	@cp bcmdrivers/broadcom/char/adsl/impl1/softdsl/AdslXfaceData.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/softdsl/
	@cp bcmdrivers/broadcom/char/adsl/impl1/softdsl/CircBuf.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/softdsl/
	@mkdir -p $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/adslcore6348 
	@cp bcmdrivers/broadcom/char/adsl/impl1/adslcore6348/adsl_defs.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/adslcore6348 
	@mkdir -p $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/adslcore6348B 
	@cp bcmdrivers/broadcom/char/adsl/impl1/adslcore6348B/adsl_defs.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/adslcore6348B
	@mkdir -p $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/adslcore6348C
	@cp bcmdrivers/broadcom/char/adsl/impl1/adslcore6348C/adsl_defs.h $(GPL_DIR)/kernel/bcmdrivers/broadcom/char/adsl/impl1/adslcore6348C
#-- linux config --
	sed 's/CONFIG_BCM_ENET=m/# CONFIG_BCM_ENET is not set/' $(GPL_DIR)/kernel/kernel/linux/.config >  $(GPL_DIR)/kernel/kernel/linux/.config2
	sed 's/CONFIG_BCM_WLAN=m/# CONFIG_BCM_WLAN is not set/' $(GPL_DIR)/kernel/kernel/linux/.config2 >  $(GPL_DIR)/kernel/kernel/linux/.config3
	sed 's/CONFIG_BCM_ADSL=m/# CONFIG_BCM_ADSL is not set/' $(GPL_DIR)/kernel/kernel/linux/.config3 >       $(GPL_DIR)/kernel/kernel/linux/.config4 
	sed 's/CONFIG_BCM_ATMAPI=m/# CONFIG_BCM_ATMAPI is not set/' $(GPL_DIR)/kernel/kernel/linux/.config4 >   $(GPL_DIR)/kernel/kernel/linux/.config  

	rm -f $(GPL_DIR)/kernel/kernel/linux/.config2
	rm -f $(GPL_DIR)/kernel/kernel/linux/.config3
	rm -f $(GPL_DIR)/kernel/kernel/linux/.config4
	rm -f $(GPL_DIR)/kernel/kernel/linux/.config_4M
	rm -f $(GPL_DIR)/kernel/kernel/linux/.config_8M

	echo PROJ_DIR_KERNEL_SRC=kernel/kernel/linux >> $(GPL_DIR)/Rules.mak
	echo "export PROJ_DIR_KERNEL_SRC" >> $(GPL_DIR)/Rules.mak
	cp vmlinux.lz $(GPL_DIR)/

sc_kernel:  vmlinux.lz_only  
	@echo -e "\\e[36m -- make kernel done --\e[0m"	
	@echo

sc_GPL_build2:
	cp vmlinux.lz ../vmlinux.lz
        
sc_GPL_build: vmlinux.lz_only sc_GPL_build2
	@echo -e "\\e[36m -- make kernel done --\e[0m"	
	@echo

sc_modules:
	@echo -e "\\e[36m -- make kernel modules --\e[0m"	
	@echo

#-------------------------------------------
sc_driver: sc_prepare sc_modules sc_modules_install adsl_sc 
	@echo BUILD_DIR=$(BUILD_DIR)
	@echo FS_DIR=$(FS_DIR)
	@echo BCM_DIR=$(BCM_DIR)                
	@echo SC_LIB_PATH=$(SC_LIB_PATH)

	cp -af $(BCM_DIR)/pktflow.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/bcmxtmcfg.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/bcmxtmrtdrv.ko $(FS_DIR)/lib/modules/
ifeq ($(strip $(BRCM_DRIVER_FAP)),m)
	cp -af $(BCM_DIR)/bcmfap.ko $(FS_DIR)/lib/modules/
endif
	cp -af $(BCM_DIR)/adsldd.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/bcm_enet.ko $(FS_DIR)/lib/modules/
	#cp -af $(BCM_DIR)/bcm_ingqos.ko $(FS_DIR)/lib/modules/
	#cp -af $(BCM_DIR)/bcm_bpm.ko $(FS_DIR)/lib/modules/
	#cp -af $(BCM_DIR)/bcmarl.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa_cmd_tm.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa_cmd_spdsvc.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa_cmd_drv.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa_cmd_ds_wan_udp_filter.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa_gpl.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa_mw.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/rdpa.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/bdmf.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/pktrunner.ko  $(FS_DIR)/lib/modules/      
	cp -af $(BCM_DIR)/pwrmngtd.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/chipinfo.ko $(FS_DIR)/lib/modules/
	#cp -af $(BCM_DIR)/p8021ag.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/nciTMSkmod.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/ohci-hcd.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/ehci-hcd.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/libata.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/libahci.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/ahci.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/ahci_platform.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/xhci-hcd.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/usblp.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/bcm63xx_sata.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/bcm63xx_usb.ko  $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/tun.ko  $(FS_DIR)/lib/modules/	

#	cp -af $(BCM_DIR)/wlctl $(FS_DIR)/usr/sbin/
#	ln -sf wlctl $(FS_DIR)/usr/sbin/wl
#	cp -af $(BCM_DIR)/nas $(FS_DIR)/usr/sbin/nas
#	cp -af $(BCM_DIR)/acsd $(FS_DIR)/usr/sbin/acsd
#	cp -af $(BCM_DIR)/acs_cli $(FS_DIR)/usr/sbin/acs_cli
	$(STRIP) -g  $(BCM_DIR)/wl.ko 
#	$(STRIP) -g  $(BCM_DIR)/wl.ko $(BCM_DIR)/libwlbcmcrypto.so $(BCM_DIR)/libwlctl.so $(BCM_DIR)/libwps.so
#	$(STRIP) -g  $(BCM_DIR)/wps_monitor
	cp -af $(BCM_DIR)/wl.ko $(FS_DIR)/lib/modules/
#	cp -af $(BCM_DIR)/libwlctl.so $(FS_DIR)/lib/
	cp -af $(BCM_DIR)/wfd.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/wlemf.ko $(FS_DIR)/lib/modules/
	cp -af $(BCM_DIR)/wlcsm.ko $(FS_DIR)/lib/modules/

#	cp -af $(BCM_DIR)/libwlbcmcrypto.so $(FS_DIR)/lib/
#	cp -af $(BCM_DIR)/libwlbcmshared.so $(FS_DIR)/lib/
#	cp -af $(BCM_DIR)/sc_priority.ko $(FS_DIR)/lib/modules/

	@if [ -e $(BCM_DIR)/libwlbcmcrypto.so ]; then\
		echo "copy libwlbcmcrypto.so from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/libwlbcmcrypto.so $(FS_DIR)/lib/;\
	fi
	@if [ -e $(BCM_DIR)/libwlbcmshared.so ]; then\
		echo "copy libwlbcmshared.so from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/libwlbcmshared.so $(FS_DIR)/lib/;\
	fi
#	@if [ -e $(FS_DIR)/usr/etc/wlan ]; then\
#		rm -rf $(FS_DIR)/usr/etc/wlan ;\
#	fi
#	@if [ -e $(BCM_DIR)/wsc_config_1a_ap.txt ]; then\
		cp -af $(BCM_DIR)/wsc_config_1a_ap.txt $(FS_DIR)/usr/etc ;\
	fi
	@if [ -e $(BCM_DIR)/wps_monitor	]; then\
		cp -af $(BCM_DIR)/wps_monitor  $(FS_DIR)/usr/sbin ;\
	fi

	@if [ -e $(BCM_DIR)/libwps.so ]; then\
		echo "copy libwps.so from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/libwps.so $(FS_DIR)/lib/libwps.so;\
	fi
	@if [ -e $(BCM_DIR)/libwlupnp.so ]; then\
		echo "copy libwlupnp.so from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/libwlupnp.so $(FS_DIR)/lib/;\
	fi
	@if [ -e $(BCM_DIR)/igd ]; then\
		echo "copy igd from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/igd $(FS_DIR)/usr/sbin;\
	fi
	@if [ -e $(BCM_DIR)/libupnp ]; then\
		echo "copy bcmupnp from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/libwlupnp.so $(FS_DIR)/lib/;\
	fi
	@if [ -e $(BCM_DIR)/eapd ]; then\
		echo "copy eapd from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/eapd $(FS_DIR)/usr/sbin/eapd;\
	fi
	@if [ -e $(BCM_DIR)/lld2d ]; then\
		echo "copy lld2d from $(BCM_DIR) to $(FS_DIR)";\
		cp -af $(BCM_DIR)/lld2d $(FS_DIR)/usr/sbin/lld2;\
	fi

#	mkdir -p $(FS_DIR)/usr/etc/wlan/;\
#	cp -af $(BCM_DIR)/wlan_map/bcm*_map.bin $(FS_DIR)/usr/etc/wlan/

	cp -af $(BCM_DIR)/netfilter/*.ko $(FS_DIR)/lib/modules/
#	cp -af $(BCM_DIR)/ts_bm.ko $(FS_DIR)/lib/modules/

#	cp -af $(BCM_DIR)/libcms_msg.so $(FS_DIR)/lib
#	cp -af $(BCM_DIR)/libcms_util.so $(FS_DIR)/lib
#	cp -af $(BCM_DIR)/libcms_boardctl.so $(FS_DIR)/lib
#
#	mkdir -p $(FS_DIR)/usr/etc/adsl/
#	cp -af $(BCM_DIR)/adsl_phy.bin $(FS_DIR)/usr/etc/adsl/
#	cp -af $(BCM_DIR)/xdslctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/adslctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/libxdslctl.so $(FS_DIR)/lib/
#	cp -af $(BCM_DIR)/xtmctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/xtm $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/libatmctl.so $(FS_DIR)/lib
#	cp -af $(BCM_DIR)/ethctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/ethswctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/libwlcsm.so $(FS_DIR)/lib	

#	cp $(BCM_DIR)/br_qos.ko $(FS_DIR)/lib/modules/

	cp -af $(BCM_DIR)/bcmvlan.ko $(FS_DIR)/lib/modules/
#	cp -af $(BCM_DIR)/vlanctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/libvlanctl.so $(FS_DIR)/lib/
#	cp -af $(BCM_DIR)/libethswctl.so $(FS_DIR)/lib/
#	cp -af $(BCM_DIR)/libtmctl.so $(FS_DIR)/lib/
#	cp -af $(BCM_DIR)/librdpactl.so $(FS_DIR)/lib/
ifneq ($(strip $(BUILD_SWMDK)),)
#	cp -af $(BCM_DIR)/swmdk $(FS_DIR)/usr/sbin
endif
#	cp -af $(BCM_DIR)/spuctl $(FS_DIR)/bin/
#	cp -af $(BCM_DIR)/libspuctl.so $(FS_DIR)/lib/

#	cp -af $(BCM_DIR)/{fc,fcctl} $(FS_DIR)/bin
#	cp -af $(BCM_DIR)/libfcctl.so $(FS_DIR)/lib

#	cp -af $(BCM_DIR)/fapctl $(FS_DIR)/bin
#	cp -af $(BCM_DIR)/libfapctl.so $(FS_DIR)/lib

#ifneq ($(strip $(BUILD_VODSL)),)
#	cp -af $(BCM_DIR)/endpointdd.ko $(FS_DIR)/lib/modules
##	cp -af $(BCM_DIR)/dspdd.ko $(FS_DIR)/lib/modules
#ifneq ($(strip $(BRCM_VODSL_DECT)),)
##	cp -af $(BCM_DIR)/dect.ko $(FS_DIR)/lib/modules
#endif
#	cp -af $(BCM_DIR)/vodsl $(FS_DIR)/usr/sbin
#	cp -af $(BCM_DIR)/voctl $(FS_DIR)/usr/sbin
#endif

#3g support modules
	#cp -af $(BCM_DIR)/usbserial.ko $(FS_DIR)/lib/modules
	#cp -af $(BCM_DIR)/option.ko $(FS_DIR)/lib/modules
	#cp -af $(BCM_DIR)/usbserial_filter.ko $(FS_DIR)/lib/modules
#readyshare remote modules	
	#cp -af $(BCM_DIR)/tun.ko $(FS_DIR)/lib/modules
	@echo -e "\\e[36m -- sc_driver --\e[0m"	

vmlinux.lz_only : prebuild_checks profile_saved_check sanity_check \
      create_install kernelbuild modbuild kernelbuildlite hosttools vmlinux.lz_gen

vmlinux.lz_gen:
	@rm -f ./vmlinux
	@rm -f ./vmlinux.bin
	@rm -f ./vmlinux.lz
	cp $(KERNEL_DIR)/vmlinux . ; \
	$(STRIP) --remove-section=.note --remove-section=.comment vmlinux; \
	$(OBJCOPY) -O binary vmlinux vmlinux.bin; \
	$(HOSTTOOLS_DIR)/cmplzma -k -1 vmlinux vmlinux.bin vmlinux.lz

sc_prepare:
	if [ -d $(BCM_DIR) ]; then\
		rm -rf $(BCM_DIR);\
		mkdir $(BCM_DIR);\
	else\
		mkdir $(BCM_DIR);\
	fi

adsl_sc:
	cp  $(BRCMDRIVERS_DIR)/broadcom/char/adsl/bcm9$(BRCM_CHIP)/adsl_phy.bin $(BCM_DIR)

ifneq ($(strip $(BUILD_VODSL)),)
vodsl_sc:
	$(MAKE) -C $(BUILD_DIR)/userspace/private/apps/vodsl sc
voctl_sc:
	$(MAKE) -C $(SC_ROOT)/apps/voctl clean
	$(MAKE) -C $(SC_ROOT)/apps/voctl
else
vodsl_sc voctl_sc:
	@echo "voice not enabled, skip building $@"
endif

sc_modules_install:
# net
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/pktflow/bcm9$(BRCM_CHIP)/pktflow.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/xtmcfg/bcm9$(BRCM_CHIP)/bcmxtmcfg.ko $(BCM_DIR)
ifeq ($(strip $(BRCM_DRIVER_FAP)),m)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/fap/bcm9$(BRCM_CHIP)/bcmfap.ko $(BCM_DIR)
endif
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/adsl/bcm9$(BRCM_CHIP)/adsldd.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/vlan/bcm9$(BRCM_CHIP)/bcmvlan.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/net/enet/bcm9$(BRCM_CHIP)/bcm_enet.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/net/xtmrt/bcm9$(BRCM_CHIP)/bcmxtmrtdrv.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/net/wl/bcm9$(BRCM_CHIP)/wl.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/net/wl/bcm9$(BRCM_CHIP)/wlcsm.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/net/wl/bcm9$(BRCM_CHIP)/wlemf.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/net/wfd/bcm9$(BRCM_CHIP)/wfd.ko $(BCM_DIR)
	#cp -af $(BRCMDRIVERS_DIR)/broadcom/char/ingqos/bcm9$(BRCM_CHIP)/bcm_ingqos.ko $(BCM_DIR)
	#cp -af $(BRCMDRIVERS_DIR)/broadcom/char/bpm/bcm9$(BRCM_CHIP)/bcm_bpm.ko $(BCM_DIR)
	#cp -af $(BRCMDRIVERS_DIR)/broadcom/char/arl/bcm9$(BRCM_CHIP)/bcmarl.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/pwrmngt/bcm9$(BRCM_CHIP)/pwrmngtd.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/chipinfo/impl1/chipinfo.ko $(BCM_DIR)
	#cp -af $(BRCMDRIVERS_DIR)/broadcom/char/p8021ag/impl1/p8021ag.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/tms/impl1/nciTMSkmod.ko $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/usb/host/ohci-hcd.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/usb/host/ehci-hcd.ko  $(BCM_DIR)
# D7000 need kernel module for ether switch
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/rdpa/impl1/rdpa.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/rdpa_drv/impl1/rdpa_cmd_tm.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/rdpa_drv/impl1/rdpa_cmd_spdsvc.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/rdpa_drv/impl1/rdpa_cmd_drv.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/rdpa_drv/impl1/rdpa_cmd_ds_wan_udp_filter.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/rdpa_gpl/impl1/rdpa_gpl.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/rdpa_mw/impl1/rdpa_mw.ko  $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/opensource/char/bdmf/impl1/bdmf.ko  $(BCM_DIR) 
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/pktrunner/impl2/pktrunner.ko  $(BCM_DIR)       
# D7000 need kernel module for USB
	cp -af $(KERNEL_DIR)/drivers/ata/libata.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/ata/libahci.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/ata/ahci.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/ata/ahci_platform.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/usb/host/xhci-hcd.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/drivers/usb/class/usblp.ko $(BCM_DIR)
	cp -af $(KERNEL_DIR)/arch/arm/plat-bcm63xx/bcm63xx_sata.ko  $(BCM_DIR)
	cp -af $(KERNEL_DIR)/arch/arm/plat-bcm63xx/bcm63xx_usb.ko  $(BCM_DIR)	
# openvpn 
	cp -af $(KERNEL_DIR)/drivers/net/tun.ko  $(BCM_DIR)
# netfilter
	mkdir $(BCM_DIR)/netfilter; \
	cp -af $(KERNEL_DIR)/net/netfilter/*.ko $(BCM_DIR)/netfilter;\
	cp -af $(KERNEL_DIR)/net/ipv4/netfilter/*.ko $(BCM_DIR)/netfilter;\
	cp -af $(KERNEL_DIR)/net/ipv6/netfilter/*.ko $(BCM_DIR)/netfilter;\
#	cp -af $(KERNEL_DIR)/lib/ts_bm.ko $(BCM_DIR)
# qos
#	cp -af $(BRCMDRIVERS_DIR)/broadcom/sc_priority/sc_priority.ko $(BCM_DIR)
#	cp -af $(KERNEL_DIR)/net/bridge/br_qos.ko $(BCM_DIR)
# voice
ifneq ($(strip $(BUILD_VODSL)),)
#	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/dspapp/bcm9$(BRCM_CHIP)/dspdd.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/endpoint/bcm9$(BRCM_CHIP)/endpointdd.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/endpoint/bcm9$(BRCM_CHIP)/endptdrv/endptdrv.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/endpoint/bcm9$(BRCM_CHIP)/haushost/haushost.ko $(BCM_DIR)
	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/endpoint/bcm9$(BRCM_CHIP)/gwcommon/gwcommon.ko $(BCM_DIR)
ifneq ($(strip $(BRCM_VODSL_DECT)),)
#	cp -af $(BRCMDRIVERS_DIR)/broadcom/char/dect/bcm9$(BRCM_CHIP)/dect.ko $(BCM_DIR)
endif
endif
#3G support
	#cp -af $(KERNEL_DIR)/drivers/usb/serial/*.ko $(BCM_DIR)
#readyshare remote 
	#cp -af $(KERNEL_DIR)/drivers/net/tun.ko $(BCM_DIR)
	echo "Stripping kernel modules..."
# Modules that need parameters cannot be stripped
	eval "find $(BCM_DIR) -name '*.ko' ! -name 'ip*.ko' |xargs $(STRIP) --strip-unneeded"
# However do strip debug symbols, in case debug symbols are included
#	eval "find $(BCM_DIR) -name 'ip*.ko' |xargs $(STRIP) --strip-debug"

show:
	@echo Profile=$(PROFILE)  
	@echo WL=$(WL)  

sc_help:
#	@more Readme_allen.txt
	@echo
	@echo "-----------------------------------------------------------"
	@echo "make sc_adsl: display usage to change ATM driver/ adsl phy"


sc_adsl:
	@echo " CMD             ATM driver             ADSL phy"
	@echo " -------------   --------------------   -----------"
	@echo " make sc_adsl_1: 4.02.01 org            4.02.01 org"

	@echo		
	@echo	"Reference:"	
	@echo " DGN3300A    AdslDrv_Linux3_A2x020e  a2pb023b"

.PHONY: sc_adsl_rm_link
	
sc_adsl_rm_link:
	rm -f bcmdrivers/broadcom/char/adsl/impl1
	rm -f bcmdrivers/broadcom/include/bcm963xx
	rm -f userspace/private/apps/xdslctl
    
sc_adsl_1: sc_adsl_rm_link
	@echo " ... link to 4.02L.01 original code ... "
#	ln -sf impl1_org bcmdrivers/broadcom/char/adsl/impl1
	ln -sf impl1_20k_rc2 bcmdrivers/broadcom/char/adsl/impl1
	ln -sf bcm963xx_org bcmdrivers/broadcom/include/bcm963xx
	ln -sf xdslctl_org userspace/private/apps/xdslctl
    
_chmod:
	@chmod 774 $(TARGETS_DIR)/buildFS

cfg_nd33: _chmod sc_adsl_1
	rm -f targets/96358GW/96358GW
	cp targets/96358GW/96358GWA  targets/96358GW/96358GW
	cp kernel/linux/.config_ND kernel/linux/.config
	rm -f shared/opensource/include/bcm963xx/bcm_hwdefs.h
	ln -sf bcm_hwdefs_8m.h shared/opensource/include/bcm963xx/bcm_hwdefs.h

cfg_nd33b: _chmod sc_adsl_1
	rm -f targets/96358GW/96358GW
	cp targets/96358GW/96358GWB  targets/96358GW/96358GW
	cp kernel/linux/.config_ND kernel/linux/.config
	rm -f shared/opensource/include/bcm963xx/bcm_hwdefs.h
	ln -sf bcm_hwdefs_8m.h shared/opensource/include/bcm963xx/bcm_hwdefs.h

cfg_nd33sp: _chmod sc_adsl_1
	rm -f targets/96358GW/96358GW
	cp targets/96358GW/96358GWA  targets/96358GW/96358GW
	cp kernel/linux/.config_ND16 kernel/linux/.config
	rm -f shared/opensource/include/bcm963xx/bcm_hwdefs.h
	ln -sf bcm_hwdefs_16m.h shared/opensource/include/bcm963xx/bcm_hwdefs.h

cfg_nd33NAsp: _chmod sc_adsl_1
	rm -f targets/96358GW/96358GW
	cp targets/96358GW/96358GWA  targets/96358GW/96358GW
	cp kernel/linux/.config_ND16 kernel/linux/.config
	rm -f shared/opensource/include/bcm963xx/bcm_hwdefs.h
	ln -sf bcm_hwdefs_16m.h shared/opensource/include/bcm963xx/bcm_hwdefs.h

cfg_nd33bsp: _chmod sc_adsl_1
	rm -f targets/96358GW/96358GW
	cp targets/96358GW/96358GWB  targets/96358GW/96358GW
	cp kernel/linux/.config_ND16 kernel/linux/.config
	rm -f shared/opensource/include/bcm963xx/bcm_hwdefs.h
	ln -sf bcm_hwdefs_16m.h shared/opensource/include/bcm963xx/bcm_hwdefs.h

clean_bcm_links:
	@echo -e "\\e[36m -- clean_bcm_links --\e[0m"	
	rm -f bcmdrivers/opensource/char/serial/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/opensource/char/board/bcm963xx/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/broadcom/atm/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/broadcom/net/wl/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/broadcom/net/enet/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/broadcom/char/adsl/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/broadcom/char/atmapi/bcm9$(BRCM_CHIP)
	rm -f bcmdrivers/broadcom/char/bcmprocfs/bcm9$(BRCM_CHIP)
	

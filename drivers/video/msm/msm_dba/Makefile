obj-$(CONFIG_MSM_DBA) += msm_dba.o msm_dba_init.o msm_dba_helpers.o msm_dba_debug.o
obj-$(CONFIG_MSM_DBA_ADV7533) += adv7533.o
obj-$(CONFIG_MSM_DBA_LT8912) += lt8912.o
obj-$(CONFIG_MSM_DBA_DSI1_BRIDGE) += dba_dsi1_bridge.o
#dba_dsi1_bridge-y = dsi1_bridge.o

ifeq ($(TARGET_BUILD_VARIANT),user)
$(obj)/dba_dsi1_bridge.o: $(srctree)/$(obj)/dba_dsi1_bridge_id.user FORCE
	cp $(srctree)/$(obj)/dba_dsi1_bridge_id.user $(obj)/dba_dsi1_bridge.o
else
$(obj)/dba_dsi1_bridge.o: $(srctree)/$(obj)/dba_dsi1_bridge_id FORCE
	cp $(srctree)/$(obj)/dba_dsi1_bridge_id $(obj)/dba_dsi1_bridge.o
endif
clean:
	rm *.o

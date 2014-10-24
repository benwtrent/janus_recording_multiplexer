################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: janus_rec_postprocessor

# Tool invocations
janus_rec_postprocessor: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc  -o "janus_rec_postprocessor" $(OBJS) $(USER_OBJS) $(LIBS) `pkg-config gstreamer-app-1.0 gstreamer-1.0 --libs`
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES) janus_rec_postprocessor
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets

################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/janus_rec_postprocessor.c \
../src/rec_mux_audio_video.c 

OBJS += \
./src/janus_rec_postprocessor.o \
./src/rec_mux_audio_video.o 

C_DEPS += \
./src/janus_rec_postprocessor.d \
./src/rec_mux_audio_video.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I/usr/include/gstreamer-1.0 -O0 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" `pkg-config gstreamer-app-1.0 gstreamer-1.0 --cflags` -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



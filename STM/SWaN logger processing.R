#############################################################################################################
# SWaN STM logger processing 
# Sarah Goldsmith, August 9, 2021
# This program takes data from the HP lab STM dataloggers installed at SWaN and computes daily averages,
# then gap-fills for any missing dates using linear regression with soil moisture data from Prospect Hill 
# required datasets: SWaN STM logger files & plot key, Prospect Hill soil moisture files & plot key
############################################################################################################

#setwd("/Users/f00502n/Documents/Dartmouth/Fibox_Processing/Harvard Forest/STM")
setwd("/Users/f0046hz/Documents/GitHub/SWaN/STM")

library(dplyr)
library(tidyr)
library(ggplot2)

##### IMPORT DATA ####
#read data from our STM sensors (compiled only for dates that we were at the plots and took gas well measurements)
SWAN1 <- read.csv("compiled data 20181009 through 20210308/SWaN1_compiled_data_20181009_thru_20210308.csv", skip = 1)
SWAN2 <- read.csv("compiled data 20181009 through 20210308/SWaN2_compiled_data_20181009_thru_20210308.csv", skip = 1)
sensorkey <- read.csv("compiled data 20181009 through 20210308/SWaNLoggerSensorKey.csv")

#read data from Harvard Forest (Prospect Hill) STM sensors
ph.stm <- read.csv("HF STM/hf005-07-soil-moisture.csv") # data from 2015-2023
ph.temp <- read.csv("HF STM/hf005-04-soil-temp.csv") # data from 2015-2023
ph.sensor.key <- read.csv("HF STM/HF STM key.csv")


#### SWaN file manipulation ####
SWAN1$Box <- 1
SWAN2$Box <- 2

swan.stm <- rbind(SWAN1, SWAN2)
swan.stm <- swan.stm[,c("DateTime", "SensorID", "VWC", "Temp", "BEC","Box")]
swan.stm$Date <- as.Date(swan.stm$DateTime, format ="%m/%d/%y %H:%M", )
rm(SWAN1, SWAN2)

#merge with sensor key
swan.stm <- merge.data.frame(sensorkey, swan.stm, by.x = c("Box", "Sensor"),  by.y = c("Box", "SensorID"))
rm(sensorkey)

#adjust raw STM sensor reading to soil moisture
swan.stm$percent.moisture <- 3.879 * 10^-4 * swan.stm$VWC - 0.6956

#average daily percent moisture and temp by treatment (combining SWAN1 and 2 and combining depths)
swan.stm.avg <- swan.stm %>%
  group_by(Date, Treatment) %>%
  summarize_at(vars(percent.moisture,Temp), list(~mean(., na.rm = TRUE)))

#average daily percent moisture and temp by treatment (keeping SWAN1 and 2 separate, used only for graphs)
swan.stm.avg.by.box <- swan.stm %>%
  group_by(Date, Depth, Treatment, Box) %>%
  summarize_at(vars(percent.moisture,Temp), list(~mean(., na.rm = TRUE)))



#### Prospect Hill file manipulation ####
#calculate date from day of year for 2020 data
ph.stm.2020$date <- as.Date(ph.stm.2020$Jday, origin = "2019-12-31")
ph.stm.2020 <- subset(ph.stm.2020, select = -c(Year, Jday))

ph.stm$date <- as.Date(ph.stm$date)
ph.stm <- rbind(ph.stm, ph.stm.2020)

ph.stm <- ph.stm %>%
  pivot_longer(!date, names_to = "Sensor") #tidy data
ph.stm <- merge.data.frame(ph.stm, ph.sensor.key, by = "Sensor") #merge with key file
rm(ph.sensor.key)


#remove dates prior to 2018-10-09 and disturbance control plot
ph.stm <- ph.stm[-which(ph.stm$date <= "2018-10-09" | ph.stm$Treatment == "Disturbance Control"),]

#average percent moisture (combining all heated and all control)
ph.stm.avg <- ph.stm %>%
  group_by(date, Treatment) %>%
  summarize_at(vars(value), list(~mean(., na.rm = TRUE)))

merged.avg <- merge(ph.stm.avg, swan.stm.avg, by.x = c("date", "Treatment"), by.y = c("Date", "Treatment") )

#### plots ####
#plot HP lab STM data vs HF data
ggplot() +  
  geom_line(data = ph.stm.avg, aes(x = date, y = value, color = "red")) +
  geom_point(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, color = "blue"))+
  geom_line(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, color = "blue")) +
  facet_grid(vars(Depth), vars(Treatment)) +
  scale_colour_discrete(labels=c("SWaN STM sensor", "Prospect Hill STM sensor"))

#check stm between depths 
ggplot() +  
  geom_point(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, group = Depth, color = factor(Depth)))+
  geom_line(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, group = Depth, color = factor(Depth))) +
  facet_grid(vars(Treatment)) +
  scale_colour_discrete(labels=c("10 cm", "30 cm")) 


ggplot() +  
  geom_point(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, group = Treatment, color = factor(Treatment)))+
  geom_line(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, group = Treatment, color = factor(Treatment))) +
  facet_grid(vars(Depth)) +
  scale_colour_discrete(labels=c("Control", "Heated")) 

#just to check that the heated/control and depth temps make sense
ggplot() +  
  geom_point(data = swan.stm.avg.by.box, aes(x = Date, y = Temp, group = Depth, color = factor(Depth)))+
  geom_line(data = swan.stm.avg.by.box, aes(x = Date, y = Temp, group = Depth, color = factor(Depth))) +
  facet_grid(vars(Treatment)) +
  scale_colour_discrete(labels=c("10 cm", "30 cm")) 


ggplot() +  
  geom_point(data = swan.stm.avg.by.box, aes(x = Date, y = Temp, group = Treatment, color = factor(Treatment)))+
  geom_line(data = swan.stm.avg.by.box, aes(x = Date, y = Temp, group = Treatment, color = factor(Treatment))) +
  facet_grid(vars(Depth)) +
  scale_colour_discrete(labels=c("Control", "Heated")) 

ggplot() +  
  geom_point(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, group = Box, color = factor(Box)))+
  geom_line(data = swan.stm.avg.by.box, aes(x = Date, y = percent.moisture, group = Box, color = factor(Box))) +
  facet_grid(vars(Treatment), vars(Depth)) +
  scale_colour_discrete(labels=c("SWaN1", "SWaN2")) 

#plot control vs heated for all plots/depths
ggplot() +  
  geom_line(data = ph.stm.avg, aes(x = date, y = value, group = Treatment, color = factor(Treatment)))


#### linear regressions ####
#gapfilled-- using the daily average for each of our visit dates, combined the 10 and 30 depths but kept heated and control plots separate
#function to make a nice graph for the regressions
ggplotRegression <- function (fit) { 
  
  ggplot(fit$model, aes_string(x = names(fit$model)[2], y = names(fit$model)[1])) + 
    geom_point() +
    stat_smooth(method = "lm", col = "red") +
    labs(title = paste("Adj R2 = ",signif(summary(fit)$adj.r.squared, 5),
                       "Intercept =",signif(fit$coef[[1]],5 ),
                       " Slope =",signif(fit$coef[[2]], 5),
                       " P =",signif(summary(fit)$coef[2,4], 5)))
}


predict.values <- function(treatment.type){
  merged.avg <- merged.avg[which(merged.avg$Treatment == treatment.type),]
  print(unique(merged.avg$Treatment))
  model <- lm(percent.moisture ~ value, data = merged.avg) #linear regression of percent moisture (our measurements) vs "value" (prospect hill moisture measurement)
  print(ggplotRegression(model))
  print(summary.lm(model))
  
  prediction <- merged.avg[which(is.na(merged.avg$percent.moisture)), c("date", "value")] #find entries with NA for soil moisture
  prediction$percent.moisture <- model$coefficients[1] + model$coefficients[2] * prediction$value #calculate estimated value from linear regression
  prediction$Treatment <- treatment.type
  return(prediction)
}
  
#get some numbers! (there is definitely a neater way to do this, but I couldn't figure it out and this works)
predictions <- predict.values("Heated")
predictions <- rbind(predictions, predict.values("Control"))

#put the numbers where they should be. this is a horrifically bad way to do this!! but it does work... for now
merged.avg$percent.moisture[21] <- predictions$percent.moisture[2]
merged.avg$percent.moisture[22] <- predictions$percent.moisture[1]

write.csv(merged.avg, "Merged SWaN PH STM.csv")


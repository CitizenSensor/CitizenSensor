#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Thu Jul 19 15:05:15 2018

@author: soussi, steinmassl, stahl @ Fraunhofer EMFT; hottarek @ Fablab München

License Information:
hx711: Copyright 2019 https://github.com/tatobari
KY040: Copyright (c) 2017 Martin O'Hanlon https://github.com/martinohanlon
"""

'''
Grid for TKinter Window:
      0         1          2         3
  -----------------------------------------
0 ¦    step_name     ¦                    ¦
--¦------------------¦                    ¦
1 ¦ precaution_short ¦       Image        ¦
--¦------------------¦                    ¦
2 ¦                  ¦                    ¦
--¦   precautions_   ¦                    ¦
3 ¦       long       ¦                    ¦
--¦------------------¦                    ¦
4 ¦bt_back ¦ bt_next ¦                    ¦
--¦------------------¦--------------------¦
5 ¦       space for logos      ¦    Step_#¦
  -----------------------------------------
'''
import sys
import os
import mysql.connector as mariadb
# for IP Adress ETH0
import netifaces

#Create relative paths:
fileDir = os.path.dirname(os.path.abspath(__file__))

if os.name == 'nt': #windows
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk#Agg
if os.name == 'posix': #linux
    #on raspbian, matplotlib was installed using apt-get install python3-matplotlib
    #this uses the deprecated NavigationToolbar2TkAgg
    #handle different runtime-environments...
    try:
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk#Agg
    except:
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2TkAgg

from matplotlib.backend_bases import *
from matplotlib.figure import Figure
import usb.core #install libusb.dll and pyusb to be able to use this

import serial
import serial.tools.list_ports

if os.name == 'posix':
    #rotary encoder:
    import RPi.GPIO as GPIO
    from ky040.KY040 import KY040
    #load cell electronics
    from hx711 import HX711

from tkinter import *
import tkinter as tk
from tkinter.font import Font
import tkinter.messagebox as ms
from tkinter.ttk import  Frame
import pandas as pd
import numpy as np
import subprocess

from PIL import Image # in order to resize

from time import sleep #time is distributed with python

#global variables
step_next_index=0 #do not change
scale_factor = 0.9 #how much vertical space the image should cover
fullscreen_mode = 1
current_step=0 #makes current step accessable from outside step
bgwindow_global=0
entry_confirmed=False
choosen_concentration=0
real_concentration=0
main_menu_entry=-2 # Start with Splash-Screen ... -1 would be mainmenu
main_exit=False
# For creating calibration solutions
last_scale_val=0
mSaltUser=0
mStockUser=0

last_ADu_val = 0

if os.name == 'posix':
    #KY 040 rotary Encoder
    CLOCKPIN = 18 #CS-ConnectorBoard V1.0
    DATAPIN = 23 #CS-ConnectorBoard V1.0
    SWITCHPIN = 12 #CS-ConnectorBoard V1.0
    #HX711 load cell encoder
    SCKPIN = 6
    DTPIN = 5
    REFERENCE_UNIT = 1.94 # 1kg-Element (HOTTI)
    REFERENCE_UNIT = 12.2 # 100g-Element (Demonstrator)
    #MOTOR
    MOTORPIN = 26
    MOTORFREQ = 1000
    MOTORSTART = 40
    MOTORSET = 25
#class to create the steps and give their fuctionalities

class step:


    def __init__(self,Id,title):

        #making some variables global so that they can be used outside of step class.
        global Id_global,t,end_point_detection_estimated_time_global, current_step, entry_confirmed, bgwindow_global, main_menu_entry, last_scale_val, last_ADu_val, mSaltUser, mStockUser, nCalSol, arrConcCal, arrVolStock, arrVolWater, choice_val, step_next_index

        current_step=self
        self.clicked=0
        self.title=title
        self.Id=Id
        Id_global=self.Id
        #the Excel data sheet is read in the prog in the self.df variable
        self.df = pd.read_csv(r'%s' %os.path.join(fileDir,'Database draft.csv'), sep=';')
        self.c=self.df.columns
        #self.l is the array containig all Ids of the steps in data base
        self.l=self.df.ID.values

        for self.i in range (1,len(self.l)):

            if float(self.l[self.i]) == self.Id:
                #the parameters of the steps are read from data base in variables, i is the index of each Id in the self.l array.
                self.process_step=self.df['Process step'][self.i]
                self.group=self.df['Unnamed: 2'][self.i]
                self.user_action_required_yn=self.df['User Action Required'][self.i]
                self.user_action_required_description=self.df['Unnamed: 4'][self.i]
                self.changing_process_parameter_name=self.df['Changing process parameter'][self.i]
                self.changing_process_parameter_measurement_method=self.df['Unnamed: 6'][self.i]
                self.end_point_detection_yn=self.df['End-point detection'][self.i]
                self.end_point_detection_decription=self.df['Unnamed: 8'][self.i]
                self.end_point_detection_estimated_time=int(self.df['Unnamed: 9'][self.i])
                self.precautions_Conditions_short=self.df['Precautions/Conditions'][self.i]
                self.precautions_Conditions_long=self.df['Unnamed: 11'][self.i]
                self.imagetext=self.df['Image'][self.i]
                try:
                    self.image=os.path.join(os.path.join(fileDir, 'photos'), self.imagetext)
                except TypeError:
                    self.image=os.path.join(os.path.join(fileDir, 'photos'), 'standard.gif') #one could insert standard image here
                    pass
                #debug:
#                print(self.process_step)


        #end_point_detection_estimated_time_global=self.end_point_detection_estimated_time
        self.input=0
        self.cpos=0
        self.user_entry=""
        self.r=12
        self.tare_pressed = False
        self.target_weight = 0
        #create the tkinter window
        self.window= tk.Toplevel()
        self.tkvar = StringVar(self.window)
        self.scale_var = StringVar()
        self.choice_var = StringVar()
        self.ADu_var = StringVar()
        self.var = StringVar()

        if fullscreen_mode == 1:
            self.window.attributes('-fullscreen', True)
        self.window.attributes("-topmost", True)
        #get resolution
        self.w = int(self.window.winfo_screenwidth())
        self.h = int(self.window.winfo_screenheight())
        self.frame = Frame(self.window)
        self.frame.grid()
        self.window.event_generate('<Motion>', warp=True, x=self.window.winfo_screenwidth(), y=self.window.winfo_screenheight())
        self.window.config(cursor="none")
        #the title is given freely by the user and is displayed on the rand of the window, for example "step1"
        self.window.title(self.title)
        #window_global=self.window
        #defining font of the process_step displayed on the winodw
        if self.h > 1000:
            self.fontTextShort=Font(size=20)
            self.fontTextLong=Font(size=15,slant="italic")
            self.fontStepName=Font(size=20,slant="italic")
            self.fontFooter=Font(size=15)
            self.fontMenuCap=Font(size=30)
            self.fontMenuItem=Font(size=20)
        elif self.h < 1000 and self.h > 450:
            self.fontTextShort=Font(size=11)
            self.fontTextLong=Font(size=10,slant="italic")
            self.fontStepName=Font(size=11,slant="italic")
            self.fontFooter=Font(size=10)
            self.fontMenuCap=Font(size=20)
            self.fontMenuItem=Font(size=15)
        else:
            self.fontTextShort=Font(size=11)
            self.fontTextLong=Font(size=11,slant="italic")
            self.fontStepName=Font(size=9,slant="italic")
            self.fontFooter=Font(size=8)
            self.fontMenuCap=Font(size=30)
            self.fontMenuItem=Font(size=20)

        if self.Id not in [401,402,301,5001,598,599,7001]: # [Hauptmenü, Netzwerk-Info, Reset-choice]
            tk.Label(self.window, text=name + ': ' + self.process_step, font=self.fontStepName,wraplength=(self.w-scale_factor*self.h)).grid(column=0,row=0,columnspan=2,sticky=W) # writing process step on window
            tk.Label(self.window, text=self.precautions_Conditions_short,font=self.fontTextShort,wraplength=(self.w-scale_factor*self.h)).grid(row=1,column=0,columnspan=2,sticky=W) #text=self.user_action_required_description,font=self.fontTextLong,bg="white",width=60).grid(row=90,column=30,sticky='E')#writing user action on window
            tk.Label(self.window, text=self.precautions_Conditions_long,font=self.fontTextLong,wraplength=(self.w-scale_factor*self.h)).grid(row=3,column=0,columnspan=2,rowspan=2,sticky=W)
            tk.Label(self.window, text=('Schritt '+str(step_next_index+1)+' von '+str(num_steps)+'      '),font=self.fontFooter).grid(row=5,column=2,columnspan=2,sticky=SE)
            #self.Button1=tk.Button(self.window,text=">>",font=self.fontTextLong,command=self.execute).grid(row=4,column=1) #next
            #self.ButtonBack=tk.Button(self.window,text="<<",font=self.fontTextLong,command=self.back).grid(row=4,column=0) #back
        self.window.bind('<Return>', self.next_step)# key event "enter" to go to next step
        self.window.bind('<Escape>', self.exit_process)#ke event "Esc" to exit process

        #include photo with respect to display resolution
        #resolution is stored in w, h
        if self.process_step in ['splash']: # No scaling for splash-screen
            size = self.w, self.h
            print("*********SPLASH**********")
        else:
            size = scale_factor*self.w, scale_factor*self.h

        outfile = os.path.splitext(self.image)[0] + "_" + str(int(scale_factor*self.h)) + "_thumbnail.gif"
        if not os.path.exists(outfile):
            try:
                im = Image.open(self.image)
                im.thumbnail(size, Image.ANTIALIAS)
                im.save(outfile, "GIF")
            except IOError:
                print('cannot create thumbnail for ')
                print(r'%s' %self.image)

        self.image = outfile
        self.photo=tk.PhotoImage(file= r'%s' %self.image)
        self.photo_label=tk.Label(self.window,image=self.photo)

        if self.Id not in [401,402,301,5001,598,599,7001]: #Steps without Image
            self.photo_label.grid(row=0,rowspan=5,column=2,columnspan=2,sticky=E, padx=5, pady=5)

        # Button that describes what the switch press does, does not actually get pressed
        if self.Id in (401, 502, 5001, 598, 7001):
            self.bt_text = 'Bestätigen'
            tk.Button(self.window, text = self.bt_text).grid(row = 2, column = 2)
        elif self.Id in (301, 599):
            self.bt_text = 'Weiter'
            tk.Button(self.window, text = self.bt_text).grid(row = 2, column = 2)
        else:
            if self.Id in (5030, 503, 508): # tare 504 und 509 nicht drin, da beim Wasser abwiegen mit dem Button weitergeschaltet wird (hier sollte auch kein Tare nötig sein)
                self.bt_text = 'Tarieren'
            else:
                self.bt_text = 'Weiter'
            tk.Button(self.window, text = self.bt_text).grid(row = 4, column = 3)

        if self.Id in (401, 5001, 598, 599, 7001): # steps top and bottom label (e.g. choice or simple display steps)
            self.window.configure(background = 'white')
            self.window.grid_columnconfigure(2, minsize=480)
            self.window.grid_rowconfigure(0, minsize=100)
            self.window.grid_rowconfigure(1, minsize=110)

        ###################### HAUPTMENÜ ##########################

        if self.Id == 401:   # Hauptmenü
            self.choices = ["Kalibrierlösung herstellen","Kalibrieren","Messen","Bodenfeuchte bestimmen","Bodenprobe verkleinern","ISA herstellen","Herunterfahren","Netzwerk-Info","Update & Neustart","Kalibrierlösung\nzurücksetzen","Beenden"] # Menüpunkte des Hauptmenüs
#            self.choices.append('Debugging') # option to  append 'Debugging' for adding arbitraty step sequence
            self.choice = self.choices[self.cpos]
            self.choice_var.set("%s %%" %str(self.choice))
            tk.Label(self.window, text='Hauptmenü',width=20,height=3,font=self.fontMenuCap,bg="gray80",wraplength=(scale_factor*self.h)).grid(row=0,column=0,columnspan=4,sticky=S)
            #tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=1,column=0,columnspan=4,sticky=S)
            tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuItem,bg="white",wraplength=0).grid(row=1,column=0,columnspan=4,sticky=S)
            entry_confirmed=False
            self.get_choice()
            main_menu_entry = self.choice

        if self.Id == 402:   # Hauptmenü
            self.window.configure(background='white')
            self.photo_label.grid(row=0,column=0)
            self.window.grid_columnconfigure(0, minsize=480)
            self.window.grid_rowconfigure(0, minsize=320)
            self.window.after(4000,self.click_step)

        if self.Id == 301:   # Netzwerk-Info
            netifaces.ifaddresses('wlan0')
            try:
                myWLAN = "WLAN: " + netifaces.ifaddresses('wlan0')[netifaces.AF_INET][0]['addr']
            except:
                myWLAN = "WLAN: nicht verbunden"
            netifaces.ifaddresses('eth0')
            try:
                myLAN = "LAN: " + netifaces.ifaddresses('eth0')[netifaces.AF_INET][0]['addr']
            except:
                myLAN = "LAN: nicht verbunden"
            myIp = myWLAN + "\n" + myLAN
            self.window.configure(background='white')
            tk.Label(self.window, text='IP-Adressen',width=20,height=3,font=self.fontMenuCap,bg="dark orange",wraplength=(scale_factor*self.h)).grid(row=0,column=0,columnspan=4,sticky=S)
            #tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=1,column=0,columnspan=4,sticky=S)
            tk.Label(self.window, text=myIp,font=self.fontMenuItem,bg="white",wraplength=0).grid(row=1,column=0,columnspan=4,sticky=S)
            self.window.grid_columnconfigure(2, minsize=480)
            self.window.grid_rowconfigure(0, minsize=100)
            self.window.grid_rowconfigure(1, minsize=110)

        #if self.Id == 501:   # Kalibration start
        #    #if os.path.isfile('/home/pi/work/lastip'):
        #          with open('/home/pi/work/lastip', 'r') as myfile:
        #                  myIp = myfile.read()
        #    tk.Label(self.window, text=myIp,font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=5,column=0,sticky=W)

        if self.Id == 506:   # Schüttel-Countdown
            tk.Label(self.window, text='Warte %i Sekunden.' %self.end_point_detection_estimated_time,font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,columnspan=2,rowspan=2)                            
            self.var.set("Noch %s Sekunden" %str(self.end_point_detection_estimated_time))
            tk.Label(self.window, textvariable=self.var,font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            startfastMOTOR()
            self.countdown(self.end_point_detection_estimated_time)
            tk.Label(self.window, text=('Rührer:'),font=self.fontFooter).grid(row=5,column=0,sticky=SW)
            self.speedvar = StringVar()
            self.speedvar.set("%s %%" %str(MOTORSTART))
            tk.Label(self.window, textvariable = self.speedvar,font=self.fontFooter).grid(row=5,column=1,sticky=SE)

        if (self.Id == 501) or (self.Id == 507):
           self.target_weight = 11.5 * 1000  # Gewicht eines Bechers ohne Deckel
           self.weighing(self.target_weight)

        if self.Id == 502:  # Konzentration
            self.choices = [0.1,10,20] # Konzentrationen
            self.choice = self.choices[self.cpos]
            self.choice_var.set("%s %%" %str(self.choice))
            tk.Label(self.window, text='Wähle die Konzentration aus',font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=2,columnspan=2)
            tk.Label(self.window, text='Konzentration:',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuCap,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            entry_confirmed=False
            self.get_choice()

        if (self.Id == 503) or (self.Id == 1002):  # Salz abwiegen
            if (self.Id == 503):
               arrConcCal = arrConcDes[:] # Zurücksetzen
               nCalSol = 1 # Zurücksetzen
               self.target_weight = stdVol * arrConcCal[0] * mKNO3 / mN # 3029.2 mg
            if (self.Id == 1002):
               self.target_weight = stdVol * 1000 * 2 * mISA # für 2 M ISA Lösung in mg, stdVol is in liter!
            tk.Label(self.window, text='Wiege %.3f g Salz ab.' %(self.target_weight/1000),font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=2,columnspan=2)
            tk.Label(self.window, text='Gewicht (g):',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.scale_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            self.weighing(self.target_weight)

        if (self.Id == 504) or (self.Id == 1003): # Wasser einfüllen
            mSaltUser = last_scale_val #mg
            if self.Id == 504:
                self.target_weight = mSaltUser * mN / mKNO3 / arrConcCal[0] * 1000000 # *1000000 um Wassermenge in mg zu erhalten
            if self.Id == 1003:
                self.target_weight = mSaltUser / mISA / 2 * 1000
            tk.Label(self.window, textvariable=self.var,font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=1,column=2,rowspan=1,columnspan=2)
            tk.Label(self.window, text='Wiege %.3f g Wasser ab.' %(self.target_weight/1000),font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=1,columnspan=2)
            tk.Label(self.window, text='Gewicht (g):',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.scale_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)

            self.weighing(self.target_weight)

        if (self.Id == 505) or (self.Id == 1004): # Becher beschriften
            self.mWaterUser = last_scale_val
            if self.Id == 505:
                self.concUser5000 = mSaltUser * mN / mKNO3 / self.mWaterUser * 1000000 # ppm NO3-N
                tk.Label(self.window, text='%i ppm NO3-N\n Stammlösung' %(self.concUser5000),font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
                arrConcCal[0] = self.concUser5000 # in DB schreiben
                sql = ("INSERT INTO `CalSol-table` (valueStock,realvalueStock,value1,value2) VALUES (5000,CAST(%i AS CHAR),100,4)" %(self.concUser5000))
                cursor.execute(sql)
                nitradoDB.commit()
                set_active('CalSol-table', get_last_id('CalSol-table'))
                print(arrConcCal)
            if self.Id == 1004:
                self.concUserISA = mSaltUser / 1000 / mISA / self.mWaterUser * 1000000
                tk.Label(self.window, text='%.1f M ISA' %(self.concUserISA),font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)

        # Verdünnungen (diese Schritte werden im Prozess 4 mal aufgerufen, noCalSol erhöht sich jeweils um 1
        if self.Id == 508: # Stammlösung
            updateArrCal()
            self.target_weight = arrVolStock[nCalSol] * 1000000 # mg
            if nCalSol == 1:
                tk.Label(self.window, text='Pipettiere %.3f g Stammlösung ein.' %(self.target_weight/1000),font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=2,columnspan=2)
            else:
                tk.Label(self.window, text='Pipettiere %.3f g der %.2f ppm Lösung ein.' %(self.target_weight/1000, arrConcCal[nCalSol-1]),font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=2,columnspan=2)
            tk.Label(self.window, text='Gewicht (g):',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.scale_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            print("nCalSol= " + str(nCalSol))
            self.weighing(self.target_weight)

        if self.Id == 1005: # ISA zugeben
            self.target_weight = 2000 * stdVol / 0.1 # 2ml ISA auf 100 ml
            tk.Label(self.window, text='Pipettiere %.3f g ISA ein.' %(self.target_weight/1000),font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=2,columnspan=2)
            tk.Label(self.window, text='Gewicht (g):',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.scale_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            self.weighing(self.target_weight)

        if self.Id == 509: # Wasser einfüllen
            #self.target_weight = arrVolWater[nCalSol] * 1000000 # *1000000 um Wassermenge in mg zu erhalten
            mSaltUser = last_scale_val # mg Variablenmissbrauch
            self.target_weight = mSaltUser / arrVolStock[nCalSol] * arrVolWater[nCalSol]
            tk.Label(self.window, textvariable=self.var,font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=1,column=2,rowspan=1,columnspan=2)
            tk.Label(self.window, text='Wiege %5.2f g Wasser ab.' %(self.target_weight/1000),font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=1,columnspan=2)
            tk.Label(self.window, text='Gewicht (g):',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.scale_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            self.weighing(self.target_weight)

        if self.Id == 510: # Becher beschriften
            self.mWaterUser = last_scale_val
            self.concUser = arrConcCal[nCalSol-1] * mSaltUser / (self.mWaterUser + mSaltUser) # ppm NO3-N
            tk.Label(self.window, text='%.2f ppm NO3-N' %(self.concUser),font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            arrConcCal[nCalSol] = self.concUser # in DB schreiben
            cursor.execute("UPDATE `CalSol-table` SET realvalue%i = CAST(%.2f AS CHAR) WHERE active = b'1'" % (nCalSol, self.concUser)) #letzte Zeile updaten
            nitradoDB.commit()
            nCalSol = nCalSol + 1
            print(arrConcCal)

        if self.Id == 598: # Kalibrierlösung zurücksetzen
            self.choices = ['Werkseinstellungen', 'Neuester Wert', 'Zweitneuester Wert']
            self.choice = self.choices[1]
            self.choice_var.set(self.choice)
            tk.Label(self.window, text='Lösung zurücksetzen auf',width=20,height=3,font=self.fontMenuCap,bg="gray80",wraplength=(scale_factor*self.h)).grid(row=0,column=0,columnspan=4,sticky=S)
            tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuItem,bg="white",wraplength=0).grid(row=1,column=0,columnspan=4,sticky=S)
            entry_confirmed = False
            self.get_choice() # warning: though an infinite loop, python thread continues with script here after first loop in get_choice()

        if self.Id == 599: # Kalibrierung entsprechend Wahl in 598 konfigurieren
            if choice_val == 'Werkseinstellungen':
                set_active('CalSol-table', 1)
            elif choice_val == 'Neuester Wert':
                set_active('CalSol-table', get_last_id('CalSol-table'))
            elif choice_val == 'Zweitneuester Wert':
                set_active('CalSol-table', max(get_last_id('CalSol-table') - 1, 1))
            tk.Label(self.window, text='Zurückgesetzt auf',width=20,height=3,font=self.fontMenuCap,bg="gray80",wraplength=(scale_factor*self.h)).grid(row=0,column=0,columnspan=4,sticky=S)
            tk.Label(self.window, text=choice_val,font=self.fontMenuItem,bg="white",wraplength=0).grid(row=1,column=0,columnspan=4,sticky=S)

        if self.Id == 5001: # Abfrage, ob ISA vorhanden ist
            self.choices = ['2M ISA ist vorhanden', '2M ISA jetzt herstellen']
            self.choice = self.choices[0]
            self.choice_var.set(self.choice)
            tk.Label(self.window, text=self.precautions_Conditions_long,width=30,height=5,font=Font(size=19),bg="yellow",wraplength=(scale_factor*self.h)).grid(row=0,column=2,columnspan=4,sticky=S)
            tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuItem,bg="white",wraplength=0).grid(row=1,column=2,columnspan=4,sticky=S)
            entry_confirmed = False
            self.get_choice() # warning: though an infinite loop, python thread continues with script here after $

        if self.Id == 5010: # Start der Kalbrierlösungherstellung
            if choice_val == '2M ISA jetzt herstellen':
                # back to main menu
                step_next_index=999
                self.window.after(10,current_step.click_step)
                # start calibration process
                main_menu_entry = 5 # ISA herstellen
                entry_confirmed = True
                self.get_choice()

        if self.Id == 5030: # Waage kalibrieren
            self.var.set('')
            tk.Label(self.window, textvariable=self.var,font=self.fontTextLong,bg="white",wraplength=(scale_factor*self.h)).grid(row=0,column=2,rowspan=2,columnspan=2)
            #self.target_weight = 3800000
            self.target_weight = 100000
            tk.Label(self.window, text='Gewicht (g):',font=self.fontTextShort,bg="white",wraplength=(scale_factor*self.h)).grid(row=2,column=2,columnspan=2)
            tk.Label(self.window, textvariable=self.scale_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)
            self.weighing(self.target_weight)

        if self.Id == 7001: # Abfrage, ob kalibriert ist
            self.choices = ['Elektrode kalibriert', 'Elektrode nicht kalibriert']
            self.choice = self.choices[0]
            self.choice_var.set(self.choice)
            tk.Label(self.window, text=self.precautions_Conditions_long,width=30,height=5,font=Font(size=19),bg="yellow",wraplength=(scale_factor*self.h)).grid(row=0,column=2,columnspan=4,sticky=S)
            tk.Label(self.window, textvariable=self.choice_var,font=self.fontMenuItem,bg="white",wraplength=0).grid(row=1,column=2,columnspan=4,sticky=S)
            entry_confirmed = False
            self.get_choice() # warning: though an infinite loop, python thread continues with script here after first loop in get_choice()

        if self.Id == 701: #Start der Messung
            if choice_val == 'Elektrode nicht kalibriert':
                # back to main menu
                step_next_index=999
                self.window.after(10,current_step.click_step)
                # start calibration process
                main_menu_entry = 1 #kalibrieren
                entry_confirmed = True
                self.get_choice()

        if self.Id == 705: # Messwertanzeige
            startMOTOR()
            tk.Label(self.window, text=('Rührer:'),font=self.fontFooter).grid(row=5,column=0,sticky=SW)
            self.speedvar = StringVar()
            self.speedvar.set("%s %%" %str(MOTORSET))
            tk.Label(self.window, textvariable = self.speedvar,font=self.fontFooter).grid(row=5,column=1,sticky=SE)
            # Kalibrierpunkte holen
            cursor.execute("SELECT calVal1 FROM `CalVal-table` WHERE active = b'1'")
            self.result100 = float(cursor.fetchall()[0][0])
            cursor.execute("SELECT calVal2 FROM `CalVal-table` WHERE active = b'1'")
            self.result4 = float(cursor.fetchall()[0][0])
            cursor.execute("SELECT realvalue1 FROM `CalVal-table` WHERE active = b'1'")
            self.conc100 = float(cursor.fetchall()[0][0])
            cursor.execute("SELECT realvalue2 FROM `CalVal-table` WHERE active = b'1'")
            self.conc4 = float(cursor.fetchall()[0][0])
            self.func_m = np.log10(self.conc100/self.conc4)/(self.result100-self.result4)
            self.func_t = np.log10(self.conc100) - self.func_m*self.result100


            tk.Label(self.window, textvariable=self.ADu_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=4,column=2,columnspan=2)
            self.arrTime = np.array([])
            self.arrADu = np.array([])
            self.figADu = Figure(figsize=(2,2), dpi=50)
            self.plotADu = self.figADu.add_subplot(111)
            self.plotADu.plot(self.arrTime,self.arrADu)
            self.dataPlot = FigureCanvasTkAgg(self.figADu, master=self.window)
            self.dataPlot.draw()
            self.dataPlot.get_tk_widget().grid(row=0, rowspan = 3, column=2, columnspan = 2, sticky="nsew")
            self.valTime = 0
            self.readADu()

        if (self.Id == 7050): #saves id's of calibration data and measurement value to new line of 'meas-table'
            cursor.execute("SELECT id FROM `CalVal-table` WHERE active = b'1'")
            idCalVal = cursor.fetchall()[0][0]
            cursor.execute("INSERT INTO `meas-table` (idCalVal,measval) VALUES (%i,%.3f)" % (idCalVal,last_ADu_val))
            nitradoDB.commit()

            out_str = "%.2f g/m^2" % (last_ADu_val / 10)
            tk.Label(self.window, text = out_str,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=3,column=2,columnspan=2)

        if (self.Id == 803) or (self.Id == 805): # Kalibrierpunkte
            startMOTOR()
            tk.Label(self.window, text=('Rührer:'),font=self.fontFooter).grid(row=5,column=0,sticky=SW)
            self.speedvar = StringVar()
            self.speedvar.set("%s %%" %str(MOTORSET))
            tk.Label(self.window, textvariable = self.speedvar,font=self.fontFooter).grid(row=5,column=1,sticky=SE)
            tk.Label(self.window, textvariable=self.ADu_var,font=self.fontMenuItem,bg="white",wraplength=(scale_factor*self.h)).grid(row=4,column=2,columnspan=2)
            self.arrTime = np.array([])
            self.arrADu = np.array([])
            self.figADu = Figure(figsize=(2,2), dpi=50)
            self.plotADu = self.figADu.add_subplot(111)
            self.plotADu.plot(self.arrTime,self.arrADu)
            self.dataPlot = FigureCanvasTkAgg(self.figADu, master=self.window)
            self.dataPlot.draw()
            self.dataPlot.get_tk_widget().grid(row=0, rowspan = 3, column=2, columnspan = 2, sticky="nsew")
            self.valTime = 0
            self.readADu()

        if (self.Id == 8035) or (self.Id == 806): # Messwert Kalibrierung in DB schreiben
            if self.Id == 8035:
                cursor.execute("SELECT realvalue1 FROM `CalSol-table` WHERE active = b'1'")
                realval1 = cursor.fetchall()[0][0]
                cursor.execute("SELECT realvalue2 FROM `CalSol-table` WHERE active = b'1'")
                realval2 = cursor.fetchall()[0][0]
                sql = ("INSERT INTO `CalVal-table` (realvalue1,realvalue2,calVal1) VALUES (%.2f,%.2f,%.3f)" %(realval1,realval2,last_ADu_val))
            if self.Id == 806:
                sql = ("UPDATE `CalVal-table` SET calVal2=CAST(%.3f AS CHAR) WHERE active = b'1'" %(last_ADu_val))
            cursor.execute(sql)
            nitradoDB.commit()
            set_active('CalVal-table', get_last_id('CalVal-table'))

    def countdown(self, remaining = None):
        if remaining is not None:
            self.remaining = remaining
        if  self.remaining <= 0:
            self.execute()
            return
        else:
            self.remaining = self.remaining - 1
            self.var.set("Noch %s Sekunden" %str(self.remaining))
            self.window.after(1000, self.countdown)

    def get_choice(self):
        global main_menu_entry
        if self.Id in [502]: #Prozentwert
            self.choice_var.set("%s %%" %str(self.choice))
        if self.Id in [401, 5001, 598, 7001]: #Nur Text
            self.choice_var.set("%s" %str(self.choice))
        if entry_confirmed:
            if self.Id == 401: # Hauptmenü
                main_menu_entry = self.cpos
            self.execute()
            return
        else:
            self.window.after(100, self.get_choice)

    def weighing(self, target = None):
        global REFERENCE_UNIT
        #print("target: %s" %str(target))
        if self.tare_pressed:
            hx.tare(50)
            self.tare_pressed = False
        if target is not None:
            hx.reset()
            hx.tare(50)
            self.scale_val = 0
        if abs(self.scale_val - self.target_weight) <= self.target_weight/10: # 10% max difference for valid target
            #print('target reached')
            if self.Id == 5030:
                tmp1_scale_val =hx.get_weight(100)
                tmp2_scale_val =hx.get_weight(100)
                if abs(tmp1_scale_val - tmp2_scale_val) <= 50: # max measurement-error
                    REFERENCE_UNIT = REFERENCE_UNIT * self.scale_val / self.target_weight
                    hx.set_reference_unit(REFERENCE_UNIT)
                    self.var.set('Gewicht kann entfernt werden.')
                    #print('L1')
                    self.weighing_loop(0)
                else:
                    #print('L2')
                    self.weighing_loop()
            elif (self.Id == 504) or (self.Id == 509) or (self.Id == 1003):
                self.var.set('LANGSAM!\n Um fortzufahren, Button drücken.')
                self.weighing_loop(self.target_weight)
            else:
                #print('L3')
                #print(target)
                self.weighing_loop(self.target_weight)
        else:
            #print('L4')
            self.weighing_loop()

    def weighing_loop(self, target = None):
        global last_scale_val
        self.scale_val =hx.get_weight(10)
        self.scale_var.set("%.3f" %(self.scale_val/1000))
        if target is not None:
            #if self.scale_val < (target + 10) and self.scale_val > (target - 10):
            #print('----')
            #print(self.scale_val - target)
            #print(target)
            #print(self.scale_val)
            #print('----')
            if abs(self.scale_val - target) <= max([target/20,100]):
                if (self.Id == 504) or (self.Id == 509) or (self.Id == 1003):
                    last_scale_val = self.scale_val
                    self.window.after(1000, self.weighing)
                else:
                    last_scale_val = self.scale_val
                    print(last_scale_val)
                    self.execute()
                return
            #self.window.after(1000, self.weighing_loop, 0)
            self.window.after(1000, self.weighing_loop, target)
        else:
            self.window.after(1000, self.weighing)

    def readADu(self):
        global last_ADu_val
        ser = serial.Serial('/dev/ttyAMA0', timeout=1)
        cmd = "p\r"
        ser.flushInput()
        ser.write(cmd.encode())
        buffer = ser.readline()
        ser.close()
        buffer = buffer.decode()
        buffer = buffer.split("\t")[1]
        self.ADu_val = float(buffer.split("\r")[0])
        if self.Id == 705: # Messwertanzeige
            self.ADu_val = np.power(10, (self.func_m*self.ADu_val*1000+self.func_t))
            self.ADu_var.set("%i ppm" %(self.ADu_val))
            #print(self.arrADu)
            self.arrADu = np.append(self.arrADu, self.ADu_val)
            last_ADu_val = self.ADu_val
            #print(self.arrTime)
            self.arrTime = np.append(self.arrTime, self.valTime)
            self.valTime = self.valTime + 2
            self.plotADu.clear()
            self.plotADu.plot(self.arrTime,self.arrADu)
            self.figADu.canvas.draw()
            self.figADu.canvas.flush_events()
            self.window.after(2000, self.readADu)
        if (self.Id == 803) or (self.Id == 805):
            self.ADu_var.set("%.1f mV" %(self.ADu_val*1000))
            #print(self.arrADu)
            self.arrADu = np.append(self.arrADu, self.ADu_val*1000)
            #print(self.arrTime)
            self.arrTime = np.append(self.arrTime, self.valTime)
            self.valTime = self.valTime + 2
            self.plotADu.clear()
            self.plotADu.plot(self.arrTime,self.arrADu)
            self.figADu.canvas.draw()
            self.figADu.canvas.flush_events()
            last_ADu_val = self.ADu_val*1000
            self.window.after(2000, self.readADu)

    #execute method is method behind the next button, to go to next step
    def execute(self):
        global step_next_index
        self.clicked=1
        #if self.clicked==1:
        stopMOTOR()
        self.window.destroy()
        #checks if index i is smaller than length of array of steps(table) given by user in process class
        if step_next_index < len(table)-1:
            #checks if usb is connected in step with id=203
            if (self.Id==203) and (not(self.usb())):
                print("Error")
                #stays in same step
                step(table[step_next_index],name)
            else:
                #moves to next step in table
                step_next_index= step_next_index +1
                step(table[step_next_index],name)

        else:
            print("EOT")
            bgwindow_global.destroy()
            #self.window.destroy()
    #execute method is method behind the back button, to go to previous step
    def back(self):
        global step_next_index
        self.clicked=1
        #if self.clicked==1:

        self.window.destroy()
        #checks if index i is smaller than length of array of steps(table) given by user in process class
        if step_next_index < len(table)-1:
            #moves to previous step in table
            step_next_index= step_next_index -1
            step(table[step_next_index],name)
        else:
            bgwindow_global.destroy()
            #self.window.destroy()

    def click_step(self):
        self.execute() #Rot-Switch pressed

    def next_step(self,event):
        step.execute(self) #"enter" key is pressed, execute function is called

    def exit_process(self,event):
        bgwindow_global.destroy()
        #self.window.destroy() #Esc key is pressed, window is destroyed (exit process)

class background:
    def __init__(self):
        global bgwindow_global, fullscreen_mode
        self.window = tk.Tk()
        if fullscreen_mode == 1:
            self.window.attributes('-fullscreen', True)
        self.window.attributes("-topmost", False)
        #self.window.after_idle(self.window.attributes, "-topmost", False)
        #self.window.grid()
        #self.frame = Frame(self.window)
        #self.frame.grid()
        bgwindow_global = self.window

        self.window.bind('<Escape>', self.exit_process)  # key event "Esc" to exit process

        #get resolution
        self.w = int(self.window.winfo_screenwidth())
        self.h = int(self.window.winfo_screenheight())

        #include photo with respect to display resolution
        #resolution is stored in w, h
        size = 0.5*self.h, 0.5*self.h
        self.image = os.path.join(os.path.join(fileDir, 'photos'), 'loading.gif')
        outfile = os.path.splitext(self.image)[0] + "_" + str(int(0.5*self.h)) + "_thumbnail.gif"
        if not os.path.exists(outfile):
            try:
                im = Image.open(self.image)
                im.thumbnail(size, Image.ANTIALIAS)
                im.save(outfile, "GIF")
            except IOError:
                print('cannot create thumbnail for ')
                print(r'%s' %self.image)

        self.image = outfile
        self.photo = tk.PhotoImage(file=r'%s' %self.image)
        self.photo_label = tk.Label(self.window, image=self.photo)
        self.photo_label.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        self.window.event_generate('<Motion>', warp=True, x=self.window.winfo_screenwidth(), y=self.window.winfo_screenheight())
        self.window.config(cursor="none")

    def exit_process(self, event):
        self.window.destroy()  # Esc key is pressed, window is destroyed (exit process)


#process class takes array of steps in wished order and
class process:
    def __init__(self, array, process_name):
        global table, name, num_steps
        self.array=array
        self.process_name=process_name
        table=self.array
        name=self.process_name
        num_steps = len(table)
        #for i in range (0,len(table)):
        #    print(table[i])
        step(table[0],name) #display first step in array

if os.name == 'posix':
    # Callback for rotary change
    def rotaryChange(direction):
        print("turned - " + str(direction))
        print(current_step.Id)
        if current_step.Id in [401,5001,502,598,7001]: # Hauptmenü,Konzentration,Kalibrierungs-Reset
            if direction == 1:
                if len(current_step.choices)-1 < current_step.cpos+1:
                    current_step.cpos = 0
                else:
                    current_step.cpos += 1
            else:
                if current_step.cpos == 0:
                    current_step.cpos = len(current_step.choices)-1
                else:
                    current_step.cpos -= 1
            print("cpos:")
            print(current_step.cpos)
            current_step.choice=current_step.choices[current_step.cpos]
        if current_step.Id in [506,705,803,805]: # Magnetrührer Speed einstellen
            freq_motor = int(current_step.speedvar.get().split(" ")[0])
            if direction == 1:
                freq_motor = freq_motor + 5
                if freq_motor > 100:
                    freq_motor = 100
                current_step.speedvar.set("%s %%" %str(freq_motor))
                MOTOR.ChangeDutyCycle(freq_motor)
            else:
                freq_motor = freq_motor - 5
                if freq_motor < 10:
                    freq_motor = 10
                current_step.speedvar.set("%s %%" %str(freq_motor))
                MOTOR.ChangeDutyCycle(freq_motor)

    # Callback for switch button pressed
    def switchPressed():
        global current_step, entry_confirmed, step_next_index, choice_val
        print("button pressed")
        pressTime = time.time()
        pressDuration = 0
        while (GPIO.input(SWITCHPIN)==0) and (pressDuration <=2): # Button still pressed?
            pressDuration = time.time()-pressTime
            sleep(0.01)
        if pressDuration >= 2:
            longPress = True
        else:
            longPress = False
        if longPress == True: # Longpress -> back to Mainmenu
            stopMOTOR()
            step_next_index=999
            current_step.window.after(10,current_step.click_step)
            while (GPIO.input(SWITCHPIN)==0): # Waits until Button is released
                sleep(0.01)
            #bgwindow_global.destroy()
        else:
            #current_step.window.after(10,current_step.exit_precess)
            print("Step-ID: %i" %current_step.Id)
            sleep(0.2)
            if current_step.bt_text == 'Tarieren':
                current_step.tare_pressed=True
            elif current_step.bt_text == 'Bestätigen': # choice
                if current_step.Id in (5001,598, 7001):
                    choice_val = current_step.choice
                entry_confirmed=True
            elif current_step.bt_text == 'Weiter':
                #current_step.next_step(0)
                current_step.window.after(10,current_step.click_step)
            else:
                raise ValueError('Switch Press not bound to operation')

    #MOTOR functions
    def startMOTOR():
        print('started motor')
        MOTOR.start(MOTORSTART)
        time.sleep(0.2)
        MOTOR.ChangeDutyCycle(MOTORSET)

    def stopMOTOR():
        print('stopped motor')
        MOTOR.stop()

    def startfastMOTOR():
        MOTOR.start(MOTORSTART)

    GPIO.setmode(GPIO.BCM)
    GPIO.setwarnings(False)

def set_active(tablename, num_id):
    cursor.execute("UPDATE `%s` SET active = b'0'" % (tablename)) # overrides all active values with b'0' (0-Bit)
    cursor.execute("UPDATE `%s` SET active = b'1' WHERE id = %i" % (tablename, num_id))
    nitradoDB.commit()

def get_last_id(tablename):
    cursor.execute("SELECT id FROM `%s` ORDER BY id DESC LIMIT 1" % (tablename))
    return cursor.fetchall()[0][0]

if __name__ == "__main__":

    if os.name == 'posix':
        ky040 = KY040(CLOCKPIN, DATAPIN, SWITCHPIN, rotaryChange, switchPressed)
        ky040.start()
        GPIO.setup(CLOCKPIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        GPIO.setup(DATAPIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        #GPIO.setup(SWITCHPIN, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)
        hx = HX711(DTPIN,SCKPIN)
        hx.set_reading_format("MSB", "MSB")
        hx.set_reference_unit(REFERENCE_UNIT)
        hx.reset() #commented out, to speed up startup
        hx.tare()  #commented out, to speed up startup
        #MOTOR
        GPIO.setup(MOTORPIN, GPIO.OUT)
        MOTOR = GPIO.PWM(MOTORPIN, MOTORFREQ)

    #Database-Stuff
    nitradoDB = mariadb.connect(user='nitrado', password='nitrado', database='nitrado')
    cursor = nitradoDB.cursor()

    ####DB-DEMOS
    #create test-table and demo-data-entrys (if not exists)
    cursor.execute("CREATE TABLE IF NOT EXISTS `test-table` (`id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT,`name` varchar(256) NOT NULL,`value` varchar(256) NOT NULL,`created` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,`modified` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")
    cursor.execute("INSERT IGNORE INTO `test-table` (id,name,value) VALUES (1,'projekt','nitrado')")
    cursor.execute("INSERT IGNORE INTO `test-table` (id,name,value) VALUES (2,'version','0.9')")
    cursor.execute("INSERT IGNORE INTO `test-table` (id,name,value) VALUES (3,'startcounter','0')")
    nitradoDB.commit()
    #select query
    cursor.execute("SELECT name,value FROM `test-table` WHERE name='projekt'")
    for name,value in cursor:
        print ("DB-TEST:  NAME : " + name + " VALUE: " + value);
    #update query
    cursor.execute("UPDATE `test-table` SET value=CAST(CAST(value AS UNSIGNED)+1 AS CHAR) WHERE name='startcounter'")
    nitradoDB.commit()
    #insert query
    cursor.execute("INSERT INTO `test-table` (name,value) VALUES (%s,%s)", ("insert-test","done"))
    nitradoDB.commit()
    #delete query
    cursor.execute("DELETE FROM `test-table` WHERE name='insert-test'")
    nitradoDB.commit()

    #Matthias/Aaron test
    # CalVal-table initialization
    try: # enters if table doesn't already exists
        cursor.execute("CREATE TABLE `CalVal-table` (`id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT,`active` BIT(1) NOT NULL,`calVal1` float NOT NULL,`realvalue1` float NOT NULL,`calVal2` float NOT NULL,`realvalue2` float NOT NULL,`created` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")
        cursor.execute("INSERT IGNORE INTO `CalVal-table` (id,calVal1,calVal2,realvalue1,realvalue2) VALUES (1,78.4,171.2,100.0,4.0)")
    except:
        pass
    else:
        set_active('CalVal-table', 1)
    nitradoDB.commit()

    # CalSol-table initialization
    try:
        cursor.execute("CREATE TABLE `CalSol-table` (`id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT,`active` BIT(1) NOT NULL,`valueStock` float NOT NULL,`realvalueStock` float NOT NULL,`value1` float NOT NULL,`realvalue1` float NOT NULL,`value2` float NOT NULL,`realvalue2` float NOT NULL,`created` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")
        cursor.execute("INSERT IGNORE INTO `CalSol-table` (id,valueStock,realvalueStock,value1,realvalue1,value2,realvalue2) VALUES (1,5000,4986,100,101.32,4,4.01)")
    except:
        pass
    else:
        set_active('CalSol-table', 1)
    nitradoDB.commit()

    # meas-table initialization
    cursor.execute("CREATE TABLE IF NOT EXISTS `meas-table` (`id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT,`idCalVal` int(10) NOT NULL,`measval` float NOT NULL,`created` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")

    #Concentration of calibraion solution Stuff
    # Variables for creating calibration solutions
    mNaNO3 = 84.9947 # g/mol
    mKNO3 = 101.1032 # g/mol
    mN = 14.0067 # g/mol
    mISA = 132.14 # g/mol

    arrConcDes = np.array([5000, 100, 4, 0.1, 0.01]) # ppm NO3-N
    #print(arrConcDes)
    arrConcCal = np.array([5000, 100, 4, 0.1, 0.01]) # ppm NO3-N
    stdVol = 0.08 # l
    # Calculating Stock and Water amounts
    nCalSol = 1

    def updateArrCal():
         global arrVolStock, arrVolWater
         arrVolStock = np.array([0.,0.,0.,0.,0.])
         arrVolWater = np.array([0.,0.,0.,0.,0.])
         arrVolWater[4] = stdVol * (arrConcCal[3] - arrConcCal[4])/arrConcCal[3]
         arrVolStock[4] = stdVol - arrVolWater[4]
         arrVolWater[3] = (stdVol + arrVolStock[4]) * (arrConcCal[2] - arrConcCal[3])/arrConcCal[2]
         arrVolStock[3] = stdVol + arrVolStock[4] - arrVolWater[3]
         arrVolWater[2] = (stdVol + arrVolStock[3]) * (arrConcCal[1] - arrConcCal[2])/arrConcCal[1]
         arrVolStock[2] = stdVol + arrVolStock[3] - arrVolWater[2]
         arrVolWater[1] = (stdVol + arrVolStock[2]) * (arrConcCal[0] - arrConcCal[1])/arrConcCal[0]
         arrVolStock[1] = stdVol + arrVolStock[2] - arrVolWater[1]
         #print(arrVolStock)
         #print(arrVolWater)

    while (main_exit == False):

        print("BACKGROUND")
        bg = background()
        print("Menu-Selection:")
        print(main_menu_entry)
        # Hier werden je nach Hauptmenü-Wahl die entsprechenden Processes instanziert
        if main_menu_entry == -2: # SplashScreen
            process1=process([402],'Splash')
        if main_menu_entry == -1: # Hauptmenü
            process1=process([401],'Hauptmenü')
        if main_menu_entry == 0: # Kalibrierlösung herstellen
            main_menu_entry = -1
            process1=process([5001,5010,5030,501,503,504,505,5060,506,5061,507,508,509,510,1005,5060,506,5061,507,508,509,510,1005,5060,506,5061], 'Kalibrierlösungen herstellen') # der Einfachheit halber erstmal nur 2 ,507,508,509,510,507,508,509,510],'Kalibrierlösung herstellen')
        if main_menu_entry == 1: # Kalibrieren
            process1=process([801,602,8015,5060,7021,604,804,803,804,807,5061,8035,5060,7021,804,805,806,605,606,5061,603,808], 'Kalibrieren')
        if main_menu_entry == 5: # ISA herstellen
            main_menu_entry = -1
            process1=process([1001,5030,501,1002,1003,1004,5060,506,5061], 'ISA herstellen')
        if main_menu_entry == 2: # Messen
            main_menu_entry == -1
            process1=process([7001,701,602,5030,702,1005,7020,5060,7021,604,804,704,705,804,605,606,5061,603,7050], 'Messen')
        if main_menu_entry == 3: # Bodenfeuchte bestimmen
            main_menu_entry == -1
            process1=process([910,911,912,913,914,915], 'Bodenfeuchte bestimmen')
        if main_menu_entry == 4: # Bodenprobe verkleinern
            main_menu_entry == -1
            process1=process([920,921,922,923,924], 'Bodenprobe verkleinern')
        if main_menu_entry == 7: # Netzwerk-Info
            main_menu_entry = -1
            process1=process([301],'Netzwerk-Info')
        if main_menu_entry == 9: # Standard-Kalibrierwerte wiederherstellen
            main_menu_entry = -1
            process1=process([598,599],'Kalibrierlösung zurücksetzen')
        if main_menu_entry == 11: # Debugging, wird aktuell nicht verwendet
            process1=process([705, 7050], 'Debugging')
        main_menu_entry=-1
        step_next_index=0 #muss zurückgesetzt werden
        bgwindow_global.lower()
        bgwindow_global.mainloop()
        print("Servus")
        if main_menu_entry == 6: # Shutdown PI
            os.system("sudo shutdown -h now")
            sys.exit()
            main_exit = True
        if main_menu_entry == 8: # Neustart
#            os.system("git pull origin develop")
            os.system("python3 main_prog_IDLE_modified.py")
            main_exit = True
        if main_menu_entry == 10: # Script beenden
            main_exit = True

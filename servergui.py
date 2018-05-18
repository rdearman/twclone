#!/usr/bin/python
from Tkinter import *

class Application(Frame):
	def __init__(self, master=None):
		Frame.__init__(self, master)
		self.grid()
		self.createWidges()
	def hello(self,a):
		print "Hello!"
		print a
	def createWidges(self):
		self.Mframe = Frame(self,borderwidth=1,relief=RAISED)
		self.Mframe.grid()
		self.file_menu()
		self.edit_menu()
		self.view_menu()
		self.help_menu()

		self.quitButton = Button(self, text="Quit", command=self.quit)
		self.quitButton.grid()
	def file_menu(self):
		file_btn = Menubutton(self.Mframe, text="File", underline=5)
		file_btn.pack(side=LEFT, padx="2m")
		file_btn.menu = Menu(file_btn)
		file_btn.menu.add_cascade(label="Save", underline=0, menu=self.listall(0))
		file_btn.menu.add_command(label="Save All", underline=5)
		file_btn.menu.add_command(label="Quit", underline=0, command=self.quit)
		file_btn["menu"] = file_btn.menu
		return file_btn
	def edit_menu(self):
		edit_btn = Menubutton(self.Mframe, text="Edit", underline=0)
		edit_btn.pack(side=LEFT, padx="2m")
		edit_btn.menu = Menu(edit_btn)
		edit_btn.menu.add_cascade(label="By Type", underline=0, menu=self.listall(1))
		edit_btn["menu"] = edit_btn.menu
		return edit_btn
	def view_menu(self):
		view_btn = Menubutton(self.Mframe, text="View", underline=0)
		view_btn.pack(side=LEFT, padx="2m")
		view_btn.menu = Menu(view_btn)
		view_btn.menu.add_cascade(label="By Type", underline=0, menu=self.listall(2))
		view_btn["menu"] = view_btn.menu
		return view_btn
	def help_menu(self):
		help_btn = Menubutton(self.Mframe, text="Help", underline=0)
		help_btn.pack(side=LEFT, padx="2m")
		help_btn.menu = Menu(help_btn)
		help_btn.menu.add_command(label="About", underline=0)
		help_btn["menu"] = help_btn.menu
		return help_btn
	def listall(self,input):
		listall_btn = Menu(self.Mframe)
		listall_btn.add_cascade(label="Players", underline=0, menu=self.chooseplayer())
		listall_btn.add_cascade(label="Ships", underline=0, menu=self.chooseship())
		listall_btn.add_cascade(label="Ports", underline=1, menu=self.chooseport())
		listall_btn.add_cascade(label="Planets", underline=2, menu=self.chooseplanet())
		listall_btn.add_command(label="Config", underline=0)
		return listall_btn
	def chooseplayer(self):
		player = Menu(self.Mframe)
		player.add_command(label="By number", underline=3)
		player.add_command(label="By name", underline=4)
		return player
	def chooseship(self):
		ship = Menu(self.Mframe)
		ship.add_command(label="By number", underline=3)
		ship.add_command(label="By name", underline=4)
		return ship	
	def chooseport(self):
		port = Menu(self.Mframe)
		port.add_command(label="By sector", underline=3)
		port.add_command(label="By name", underline=4)
		port.add_command(label="By number", underline=3)
		return port
	def chooseplanet(self):
		planet = Menu(self.Mframe)
		planet.add_command(label="By sector", underline=3)
		planet.add_command(label="By name", underline=4)
		planet.add_command(label="By number", underline=3)
		return planet

app = Application()
app.master.title("TWClone Server GUI")
app.mainloop()

﻿using Avalonia.Controls;
using Mesen.Config;
using Mesen.Debugger.Utilities;
using Mesen.Interop;
using Mesen.Localization;
using Mesen.Utilities;
using Mesen.ViewModels;
using ReactiveUI;
using ReactiveUI.Fody.Helpers;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Reactive.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Mesen.Debugger.ViewModels
{
	public class MemoryToolsDisplayOptionsViewModel : DisposableViewModel
	{
		public HexEditorConfig Config { get; }
		public int[] AvailableWidths => new int[] { 4, 8, 16, 32, 48, 64, 80, 96, 112, 128 };
		
		[Reactive] public bool ShowNesPcmDataOption { get; set; }
		[Reactive] public bool ShowNesDrawnChrRomOption { get; set; }

		public MemoryToolsDisplayOptionsViewModel()
		{
			Config = ConfigManager.Config.Debug.HexEditor;
			
			if(Design.IsDesignMode) {
				return;
			}

			AddDisposable(this.WhenAnyValue(x => x.Config.MemoryType).Subscribe(x => UpdateAvailableOptions()));
		}

		public void UpdateAvailableOptions()
		{
			ShowNesPcmDataOption = Config.MemoryType.ToCpuType() == CpuType.Nes;
			ShowNesDrawnChrRomOption = Config.MemoryType.ToCpuType() == CpuType.Nes && DebugApi.GetMemorySize(MemoryType.NesChrRom) > 0;
		}
	}
}

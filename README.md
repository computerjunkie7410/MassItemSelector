# Mass Item Selector

<https://ficsit.app/mod/MassItemSelector>

A mod for the game Satisfactory.

Quickly and easily select a large number of items at once. Especially helpful for dismantling entire factories quickly. Works great with the [Infinite Dismantle](https://ficsit.app/mod/InfiniteDismantle) and [Quick Blueprint](https://ficsit.app/mod/GbFuqjXs12Hw2) mods.

## Instructions

1. Press `F` to enter Dismantle mode.
2. Position yourself over the area you want to select.
3. In **Default dismantle mode**, hold `Left Alt` (rebindable in Options). The camera will freeze and a cursor will appear.
4. While holding the lasso key, hold the left mouse button and drag around the buildings you want to select. Release the mouse button or lasso key to finish the outline and preview the results.
5. Adjust the distance setting using your mouse wheel. The distance controls appear in the upper-left corner of your screen by default; their position is configurable in Mod Options.
6. Left-click once to add matching buildings to the dismantle selection, subject to the current filter and selection limit.
7. With at least two buildings selected in Default dismantle mode, press `N` (or your configured key) to open the Advanced Dismantle Filter panel. Choose the building types to keep, then click **Apply**. Closing the panel before applying discards pending edits; closing it after applying lets the update finish in the background.

The number of buildings you can select depends on the dismantle limit. Pair this mod with Infinite Dismantle to select larger groups of buildings; the 500-meter range, building eligibility, and native filters still apply. Pair it with Quick Blueprint to quickly make or edit blueprints.

### Mod Settings


| Setting | Default | Description | Where to change |
| --- | --- | --- | --- |
| Lasso selection | Left Alt | Hold to draw a lasso. Press again while previewing to redraw. | Options → Controls → Mass Item Selector |
| Advanced Dismantle Filter | N | Open or close the filter for selected building types in Default dismantle mode. | Options → Controls → Mass Item Selector |
| Wheel step (m) | 5 meters | Distance adjustment per mouse-wheel step. Accepts 0.1–500 meters; the total lasso range is limited to 5–500 meters. | Mod Options → Mass Item Selector |
| Distance panel side | Left | Place distance controls on the left or right. Applies when the lasso is reopened. | Mod Options → Mass Item Selector |
| Selection panel side | Left | Place the Advanced Dismantle Filter on the left or right. Applies immediately. | Mod Options → Mass Item Selector |

## Mass Item Selector for Dismantling

<video width="640" controls muted loop>
  <source src="https://i.imgur.com/HwbiF4S.mp4" type="video/mp4">
</video>

## Mass Item Selector with Quick Blueprint

<video width="640" controls muted loop>
  <source src="https://i.imgur.com/cHm61pD.mp4" type="video/mp4">
</video>

## Version 2.0.0

* Made lasso a part of Default dismantle mode.
* Performance and quality-of-life improvements.

## Version 1.2.0

Upgrade minimum game build to 520140


## Version 1.1.0

Added **Advanced Dismantle Filter**. Select two or more buildings and press `N` to see them grouped by type, with a count for each. Uncheck a type to leave those buildings out, or recheck it to bring them back, then click **Apply**. Works with both the lasso and normal dismantle selections.

The panel appears on the left by default and can be moved to the right in Mod Options.

![Advanced Dismantle Filter showing selected buildings grouped by type](https://i.imgur.com/wD9Zvhj.png)

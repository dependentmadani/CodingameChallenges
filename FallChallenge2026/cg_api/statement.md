![Wood League](//cdn.codingame.com/smash-the-code/statement/league_wood_04.png)

Welcome to the Wood1 league!

In Wood 1 you can now ATTACK houses
To have a look at the final rules, go [here](https://www.codingame.com/ide/demo/843364c35de5a48f4e0de3a6e0720b74d11b1a) and switch leagues at the top right corner.

# The Goal

Collect more points than your opponent by capturing and defending triangles.

# Rules

The map consists of houses, some of which are connected by paths. Houses are defined by x and y coordinate. Each player starts with `3` units on one side of the map. Then the players try to capture a triangle by owning all three corner houses of it. They can use a triangle to spawn a new unit, kill all opponent units on a given house and add or remove a path between two houses.

**Moving units**
Units can be moved from a house to one of the direct neighbors each turn.

**House ownership**
A house is owned, when the player has strictly more units on it than the opponent player or when all neighboring houses are owned by that player.

**Surrounded units**
When a unit is surrounded by opponent houses, meaning each neighboring house is owned by the opponent, the unit is killed.

**Triangles**
A triangle is defined by three houses, that are all connected with each other. A triangle may not contain any other houses inside itself.
After each turn the players get `1` point for each triangle they currently own.

**Capturing a triangle**
To capture a triangle, a player has to own all three corners of it and be eligible to claim the triangle. Initially the players can capture all triangles. This will only change after using them. A captured triangle will remain in the ownership of the player until it's used or captured by the opponent.

**Using a triangle**
When owning a triangle, a player can use it to perform one of the actions described below. This will make the triangle neutral again. The player using it can only capture it again after moving all units away from each house or after it got captured by the opponent player.

Some usages have an additional cost, meaning that the player loses a certain amount of units on each house of the triangle. If the player doesn't have enough units on each house, the triangle can't be used. In order to use a unit, it must have been there for the whole turn and not just moved or spawned there.

**Spawning a unit**
One way to use a triangle is to spawn a unit. This will place a unit on one house of the triangle. The player can choose the corner to spawn on.  Spawning has no cost.

**Attacking a house**
A triangle can be used to attack a house, killing all opponent units on it. This action has a cost of `1`. The attacked house has to be adjacent to at least one house of the used triangle.

### Victory Conditions

- You collect more points than your opponent
- You own at least 80% of all triangles

### Loss Conditions

- You collect less points than your opponent
- You have no triangles and no units left
- Your opponent owns at least 80% of all triangles
- You do not respond in time or output an unrecognized command

# Expert Rules

The source code of the referee can be found on github: https://github.com/eulerscheZahl/TryAngle-Catch
Don't hesitate to change the viewer's options to help debug your code (![](https://www.codingame.com/servlet/fileservlet?id=3463235186409)).

The game turn works as follows:

1. Apply attack actions
2. Move units
3. Spawn units
4. Kill surrounded units
5. Change ownership of triangles

Within the same type of action, actions are applied for both players simultaneously, but sequentially for the same player.
Example:
Player 1 wants to perform two attack actions `A1a` and `A1b`. Player 2 wants to perform two attack actions `A2a` and `A2b`. First `A1a` and `A2a` will be checked for being valid (the triangles in use are owned by the respective players and they have enough units on each house) and applied if possible. Then `A1b` and `A2b` will be processed in the same way. It's possible that the first pair of actions renders one of the later ones invalid by killing units on a triangle that was supposed to be used later.

## Game Input

### Initial input

First line: `houseCount`, the number of houses on the board
Next `houseCount` lines: `houseId` `houseX` `houseY`, the id and location of each point

### Input for each game turn

First line: `myScore` `opponentScore`, the score points of you and your opponent respectively
Next `houseCount` lines: `houseId` `myUnits` `opponentUnits`, the house ID and the amount of your own and opponent units on it
Next line: `pathCount`, the number of paths
Next `pathCount` lines: `house1` `house2` indicating a connection between these two houses
Next line: `triangleCount`, the number of triangles
Next `triangleCount` lines: `house1` `house2` `house3` `owner` `meCanCapture` `opponentCanCapture`.
`house1` `house2` `house3` give the corners of the triangle.
`owner` indicating the owner of the triangle. It will be `-1` if it's neutral, `0` if it belongs to you and `1` if it belongs to your opponent.
`meCanCapture` is `1` if you can currently capture it, `0` otherwise
`opponentCanCapture` is `1` if your opponent can currently capture it, `0` otherwise
Next line: `linkableCount`, the number of new paths that can possibly be created
This value will always be `0` in lower leagues
Next `linkableCount` lines: `house1` `house2` giving the houses that can be linked.

### Output for one game turn

You can print an arbitrary amount of commands per turn, separated by `;`

- `MOVE from to amount` move `amount` units from house `from` to house `to`. If there is no direct connection between these two houses, the units will move closer along a shortest path if possible or stay if there is no path.
- `SPAWN house1 house2 house3` to spawn a unit on `house1` using the given triangle. Change the house order to affect the spawning location
- `ATTACK house1 house2 house3 target` attack the `target` house using the given triangle

### Constraints

`houseCount` ≤ `50`
`0` ≤ `houseX` < `1920`
`0` ≤ `houseY` < `1080`

Response time first turn ≤ `1000` ms
Response time per turn ≤ `50` ms
The game ends after `200` turns

Sprites:
https://craftpix.net/freebies/free-tropical-medieval-city-2d-tileset/
https://craftpix.net/freebies/2d-fantasy-knight-free-sprite-sheets/
https://craftpix.net/freebies/free-zombie-tds-tilesets-buildings-and-furniture/
[https://craftpix.net/freebies/free-different-sci-fi-item-icons](https://craftpix.net/freebies/free-different-sci-fi-item-icons/)

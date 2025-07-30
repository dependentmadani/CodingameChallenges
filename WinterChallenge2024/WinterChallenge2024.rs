//**************************************************************************
//TODO:
/*
- First 10 turns, I should harvest on C protein and A protein
*/
//**************************************************************************

use std::collections::HashMap;
use std::io;

type POSITION = (i32, i32);
type ENERGY = (i32, i32, i32, i32);

#[derive(PartialEq)]
enum OrganType {
    BASIC,
    HARVESTER,
    TENTACLE,
    SPORER,
    ROOT
}

impl OrganType {
    fn costs(&self) -> (i32, i32, i32, i32) {
        match self {
            OrganType::BASIC => (1, 0, 0, 0),
            OrganType::HARVESTER => (0, 0, 1, 1),
            OrganType::TENTACLE => (0, 1, 1, 1),
            OrganType::SPORER => (0, 1, 0, 1),
            OrganType::ROOT => (1, 1, 1, 1),
        }
    }

    fn verify(&self, organ_type_name: &str, energy: ENERGY) -> bool {
        let required_costs = self.costs(); // Get the cost of the organ type

        match organ_type_name {
            "BASIC" if *self == (OrganType::BASIC) => {
                energy.0 >= required_costs.0 
            }
            "HARVESTER" if *self == OrganType::HARVESTER => {
                energy.2 >= required_costs.2 && energy.3 >= required_costs.3 
            }
            "TENTACLE" if *self == OrganType::TENTACLE => {
                energy.1 >= required_costs.1 && energy.2 >= required_costs.2 && energy.3 >= required_costs.3
            }
            "SPORER" if *self == OrganType::SPORER => {
                energy.0 >= required_costs.0 * 2 && energy.1 >= required_costs.1 * 3 &&
                energy.2 >= required_costs.2 * 2 && energy.3 >= required_costs.3 * 3
            }
            "ROOT" if *self == OrganType::ROOT => {
                energy.0 >= required_costs.0 && energy.1 >= required_costs.1 &&
                energy.2 >= required_costs.2 && energy.3 >= required_costs.3
            }
            _ => false,
        }
    }
}

macro_rules! parse_input {
    ($x:expr, $t:ident) => {
        $x.trim().parse::<$t>().unwrap()
    };
}

fn is_position_ok(
    new_position: POSITION,
    limit: POSITION
) -> bool {
    
    if new_position.0 >= 0 && new_position.1 >= 0 &&
        new_position.0 < limit.0 && new_position.1 < limit.1 {
        return true;
    }

    false
}

fn search_for_root_basic_or_a_in_owner(
    store_positions_type: &HashMap<POSITION, String>,
    store_owner_organ: &HashMap<POSITION, i32>
) -> Vec<POSITION> {
    let mut root_basic_or_a_positions = Vec::new();
    
    // Iterate over the HashMap and find positions where the value is ROOT, BASIC, or "A"
    for (&(x, y), value) in store_positions_type.iter() {
        if (value == "ROOT" || value == "BASIC") && store_owner_organ.contains_key(&(x, y)) {
            root_basic_or_a_positions.push((x, y));
        }
    }
    
    root_basic_or_a_positions
}


// This function is responsible for finding all my root organs
fn root_organs_positions(
    store_positions_type: &HashMap<POSITION, String>,
    store_owner_organ: &HashMap<POSITION, i32>,

) -> Vec<POSITION> {
    let mut root_organ_position: Vec<POSITION> = Vec::new();
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "ROOT" && store_owner_organ.contains_key(&(x, y)) {
            root_organ_position.push((x, y));
        }
    }
    
    root_organ_position
}

// This function return the direction to target
fn calculate_direction(current: POSITION, target: POSITION) -> String {
    if current.0 == target.0 {
        if current.1 < target.1 {
            "S".to_string()
        } else {
            "N".to_string()
        }
    } else if current.0 < target.0 {
        "E".to_string()
    } else {
        "W".to_string()
    }
}


// THis will calculate the nearest organ to protein
fn find_nearest_organ(
    organ_position: &Vec<POSITION>,
    protein_position: POSITION
)-> i32 {
    let mut lowest_distance = std::i32::MAX;
    for &(x, y) in organ_position {
        let distance = manhattan_distance(protein_position, (x, y));
        if lowest_distance > distance {
            lowest_distance = distance;
        }
    }

    lowest_distance
}

// This function will propose the protein that I should go to
fn proposed_proteins(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    opp_organ_position: &Vec<POSITION>,
) -> Vec<POSITION> {
    let mut proteins: Vec<POSITION> = Vec::new();

    for (&(x, y), value) in store_positions_type.iter() {
        if value == "A" {
            if find_nearest_organ(my_organ_position, (x, y)) <= find_nearest_organ(opp_organ_position, (x, y)) {
                proteins.push((x, y));
            }
        }
    }

    for (&(x, y), value) in store_positions_type.iter() {
        if value == "B" || value == "C" || value == "D" {
            if find_nearest_organ(my_organ_position, (x, y)) <= find_nearest_organ(opp_organ_position, (x, y)) {
                proteins.push((x, y));
            }
        }
    }

    proteins
}

// This function will check the root of a specific organ
fn check_root_id(
    store_owner_organ: &HashMap<POSITION, i32>,
    organ_position: POSITION
) -> i32 {
    store_owner_organ.iter()
        .find_map(|(&key, &val)| {if key.0 == organ_position.0 && key.1 == organ_position.1 { Some(val) } else { None }})
        .unwrap_or(-1)
}

// This function will check if the position where the spore want to create root is possible
fn is_spore_position_possible(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: POSITION,
    new_root_position: POSITION
) -> bool {
    if my_organ_position.0 == new_root_position.0 {
        if my_organ_position.1 + 1 < new_root_position.1 {
            for y in (my_organ_position.1 + 1) .. (new_root_position.1) {
                // eprintln!("1 - the position x : {:?} and y {y}", my_organ_position.0);
                if store_positions_type.contains_key(&(my_organ_position.0, y)) {
                    return false;
                }
            } 
        } else {
            for y in  (new_root_position.1) .. (my_organ_position.1 + 1) {
                // eprintln!("2 - the position x : {:?} and y {y}", my_organ_position.0);
                if store_positions_type.contains_key(&(my_organ_position.0, y)) {
                    return false;
                }
            } 
        }
        return true;
    } else if my_organ_position.1 == new_root_position.1 {
        if (my_organ_position.0 + 1) < (new_root_position.0) {
            for x in (my_organ_position.0 + 1) .. (new_root_position.0) {
                // eprintln!("3 - the position x : {x} and y {:?}", my_organ_position.1);
                if store_positions_type.contains_key(&(x, my_organ_position.1)) {
                    return false;
                }
            } 
        } else {
            for x in (new_root_position.0) .. (my_organ_position.0 + 1) {
                // eprintln!("4 - the position x : {x} and y {:?}", my_organ_position.1);
                if store_positions_type.contains_key(&(x, my_organ_position.1)) {
                    return false;
                }
            } 
        }
        return true;
    }

    false
}

// Function that check whether sporer is needed or not
fn is_sporer_needed(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    proposed_protein_position: &Vec<POSITION>,
    protein_energy: ENERGY,
    width: i32,
    height: i32
) -> bool {
    let directions = [(0, 1), (0, -1), (1, 0), (-1, 0)];
    let type_organ: OrganType = OrganType::SPORER;

    for &(protein_x, protein_y) in proposed_protein_position {
        for &(organ_x, organ_y) in my_organ_position {
            for &(dx, dy) in &directions {
                let new_x = organ_x + dx;
                let new_y = organ_y + dy;
                // This condition to check that the the grid position is free
                if !store_positions_type.contains_key(&(new_x, new_y)) &&  new_x >= 0 && new_y >= 0 && new_x < width && new_y < height {
                    if (protein_x - 1 == new_x || protein_x + 1 == new_x) && (new_y - protein_y).abs() > 3 && type_organ.verify("SPORER", protein_energy)
                            && (is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x - 1, protein_y)) || is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x + 1, protein_y))) {
                        return true;
                    } else if (protein_y - 1 == new_y || protein_y + 1 == new_y) && (new_x - protein_x).abs() > 3 && type_organ.verify("SPORER", protein_energy)
                            && (is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x, protein_y - 1)) || is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x, protein_y + 1))) {
                        return true;
                    }
                }
            }
        }
    }

    false
}

// Function that return the position of sporer
fn sporer_position(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    proposed_protein_position: &Vec<POSITION>,
    protein_energy: ENERGY,
    width: i32,
    height: i32
) -> Option<(POSITION, POSITION, POSITION, String)> {
    let directions = [(0, 1), (0, -1), (1, 0), (-1, 0)];
    let type_organ: OrganType = OrganType::SPORER;

    // eprintln!("the proposed protein positions: {:?}", proposed_protein_position);
    for &(protein_x, protein_y) in proposed_protein_position {
        for &(organ_x, organ_y) in my_organ_position {
            for &(dx, dy) in &directions {
                let new_x = organ_x + dx;
                let new_y = organ_y + dy;

                // This condition to check that the the grid position is free
                if !store_positions_type.contains_key(&(new_x, new_y)) &&  new_x >= 0 && new_y >= 0 && new_x < width && new_y < height {

                    if (protein_x - 1 == new_x || protein_x + 1 == new_x) && (new_y - protein_y).abs() > 3 && type_organ.verify("SPORER", protein_energy)
                    && (is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x - 1, protein_y)) || is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x + 1, protein_y))) {
                        if new_y - protein_y < 0 {
                            return Some(((new_x, protein_y - 1), (new_x, new_y), (organ_x, organ_y), "S".to_string()));
                        } else if new_y - protein_y > 0 {
                            return Some(((new_x, protein_y -1), (new_x, new_y), (organ_x, organ_y), "N".to_string()));
                        }
                    } else if (protein_y - 1 == new_y || protein_y + 1 == new_y) && (new_x - protein_x).abs() > 3  && type_organ.verify("SPORER", protein_energy)
                    && (is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x, protein_y - 1)) || is_spore_position_possible(store_positions_type, (new_x, new_y),(protein_x, protein_y + 1))) {
                        if new_x - protein_x < 0 {
                            return Some(((protein_x -1, new_y), (new_x, new_y), (organ_x, organ_y), "E".to_string()));
                        } else if new_x - protein_x > 0 {
                            return Some(((protein_x -1, new_y), (new_x, new_y), (organ_x, organ_y), "W".to_string()));
                        }
                    }

                }
            }
        }
    }

    None
}


// This function is responsible for giving the id of a specific organ
fn search_for_organ_id(
    store_id_with_position: &HashMap<POSITION, i32>, 
    my_organ_position: POSITION
) -> i32 {

    eprintln!("the received position {:?}", my_organ_position);
    if let Some(&organ_id) = store_id_with_position.get(&(my_organ_position.0, my_organ_position.1)) {
        return organ_id;
    }

    -1
}

// Calculate Manhattan distance between two points
fn manhattan_distance(
    p1: POSITION, 
    p2: POSITION
) -> i32 {
    (p1.0 - p2.0).abs() + (p1.1 - p2.1).abs()
}

// This function is responsible for proposing an attack on the opponenent
fn near_opponent(
    my_organ_position: &Vec<POSITION>,
    my_energy: ENERGY,
    store_positions_type: &HashMap<POSITION, String>,
    my_positions: POSITION
) -> (bool, String, POSITION) {
    let type_organ: OrganType = OrganType::TENTACLE;
    
    if !type_organ.verify("TENTACLE", my_energy) {
        return (false, "".to_string(), (-1, -1));
    }

    for (&(x, y), value) in store_positions_type.iter() {
        if value == "BASIC" && !my_organ_position.contains(&(x, y)) {
            let distance = manhattan_distance(my_positions, (x, y));
            if distance == 1 {
                let direction = if x == my_positions.0 {
                    if y > my_positions.1 {
                        "S"
                    } else {
                        "N"
                    }
                } else {
                    if x > my_positions.0 {
                        "E"
                    } else {
                        "W"
                    }
                };
                return (true, direction.to_string(), (x, y));
            }
        }
    }

    (false, "".to_string(), (-1, -1))
}


// This function will decide if it should start attacking or not
fn should_i_attack(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    opp_organ_position: &Vec<POSITION>,
    my_energy: ENERGY,
    width: i32,
    height: i32
) -> Option<(POSITION, POSITION, String)> {
    let directions = [(0, 1), (0, -1), (1, 0), (-1, 0)];
    let type_organ = OrganType::TENTACLE;

    if type_organ.verify("TENTACLE", my_energy) {
        return None;
    }

    for &(me_x, me_y) in my_organ_position {
        for &(opp_x, opp_y) in opp_organ_position {
            for &(dx, dy) in &directions {
                let new_x = me_x + dx;
                let new_y = me_y + dy;
                let distance = manhattan_distance((new_x, new_y), (opp_x, opp_y));
                if distance >= 3 && new_x >= 0 && is_position_okay_special(&store_positions_type, (new_x, new_y), width, height) {
                    let direction = if (new_y - opp_y).abs() > (new_x - opp_x).abs() {
                        if new_y > opp_y {
                            "N"
                        } else {
                            "S"
                        }
                    } else {
                        if new_x > opp_x {
                            "E"
                        } else {
                            "W"
                        }
                    };
                    return Some(((new_x, new_y), (me_x, me_y), direction.to_string()));
                }
            }
        }
    }

    None    
}

// Function to find the next position towards "A" within a given range (max_distance = 5)
fn find_next_position_towards_a(
    my_organ_position: &Vec<POSITION>,
    opp_organ_position: &Vec<POSITION>,
    store_organ_owner:  &HashMap<POSITION, i32>,
    store_positions_type: &HashMap<POSITION, String>,
    proposed_protein_position: &Vec<POSITION>,
    my_energy: ENERGY,
    harvesting: Vec<POSITION>,
    current_position: POSITION,
    max_distance: i32,
    width: i32,
    height: i32,
) -> Option<(POSITION, POSITION, i32, String, POSITION, bool)> {
    // Get all "A" positions
    // let mut store_position: HashMap<POSITION, String> = HashMap::new();
    let mut attack = false;
    let mut harvest: POSITION = (-1, -1); 
    let mut owner: i32 = if store_organ_owner.contains_key(&current_position) {
        store_organ_owner[&current_position]
    } else {
        -1
    };

    if let Some(((new_x, new_y), (position_x, position_y), direction)) = should_i_attack(&store_positions_type, my_organ_position, opp_organ_position, my_energy, width, height) {
        attack = true;
        return Some(((new_x, new_y), (position_x, position_y), std::i32::MAX, direction, harvest, attack));
    }
    // eprintln!("check the proposed protein: {:?}", proposed_protein_position);
    // for (&(a_x, a_y), a_type) in store_positions_type {
    //     if !harvesting.contains(&(a_x, a_y)) {
    //         store_position.insert((a_x, a_y), a_type.clone());
    //     }
    // }
    // let a_positions = search_for_a_positions(&store_position);
    // eprintln!("a_positions: {:?}", a_positions);
    // Check for type A within range and find direction
    let type_organ: OrganType = OrganType::HARVESTER;
    let mut updated = false;
    let mut new_position = (0, 0);
    let mut distance = std::i32::MAX;
    let mut min_distance: i32 = std::i32::MAX;
    let mut protein_position: (i32, i32) = (-1, -1);

    for &(a_x, a_y) in proposed_protein_position {
        distance = manhattan_distance(current_position, (a_x, a_y));

        if distance <= max_distance && distance != 1 {
            // eprintln!("that was the first condition {:?}", (a_x, a_y));
            // Calculate the direction
            
            let directions = [(0, 1), (0, -1), (1, 0), (-1, 0)];
            // distance = std::i32::MAX;
            for &(dx, dy) in &directions {
                let new_x = current_position.0 + dx;
                let new_y = current_position.1 + dy;
                
                let new_distance = manhattan_distance((new_x, new_y), (a_x, a_y));
                if new_distance < min_distance && is_position_ok((new_x, new_y), (width, height)) && is_position_okay_special(&store_positions_type, (new_x, new_y), width, height){
                    // eprintln!("****************** position {new_x} {new_y}: {:?} protein: {a_x} {a_y}", is_position_okay_special(&store_positions_type, (new_x, new_y), width, height));
                    new_position = (new_x, new_y);
                    protein_position = (a_x, a_y);
                    min_distance = new_distance;
                    updated = true;
                }
            }
        }
    }
    if updated && type_organ.verify("HARVESTER", my_energy) {
        if min_distance == 1 {
            harvest = (protein_position.0, protein_position.1);
        }
        let direction = if protein_position.0 == new_position.0 {
            if protein_position.1 < new_position.1 {
                "N"
            } else {
                "S"
            }
        } else {
            if protein_position.0 < new_position.0 {
                "W"
            } else {
                "E"
            }
        };
        // eprintln!("that is weird {min_distance} {:?} {:?} {direction}", current_position, new_position);
        let check_opponent = near_opponent(my_organ_position,  my_energy, store_positions_type, new_position);
        if check_opponent.0 {
            // eprintln!("should not be here in this case");
            return Some((new_position, check_opponent.2, min_distance, check_opponent.1, harvest, check_opponent.0));
        }
        return Some((new_position, current_position, min_distance, direction.to_string(), harvest, attack));
    }

    // Fallback to default movement logic if no valid A positions within range
    if let Some((new_x, new_y)) = find_next_position_to_a(store_positions_type, my_organ_position, current_position.0, current_position.1, width, height) {
        // eprintln!("that was the second condition {new_x} {new_y}");
        let check_opponent = near_opponent(my_organ_position, my_energy, store_positions_type, (new_x, new_y));
        if check_opponent.0 {
            return Some(((new_x, new_y), current_position, std::i32::MAX, check_opponent.1, harvest, check_opponent.0));
        }
        return Some(((new_x, new_y), current_position, std::i32::MAX, "".to_string(), harvest, attack));
    }

    // eprintln!("all my organ positions: {:?} owner {:?}", my_organ_position);
    // eprintln!("the owner is: {owner}");
    for &(x, y) in my_organ_position.iter() {
        if store_organ_owner.contains_key(&(x, y)) && store_organ_owner[&(x, y)] == owner {
            if let Some((new_x, new_y)) = find_next_position_to_a(store_positions_type, my_organ_position, x, y, width, height) {
                // eprintln!("that was the third condition");
                let check_opponent = near_opponent(my_organ_position, my_energy, store_positions_type, (new_x, new_y));
                if check_opponent.0 {
                    return Some(((new_x, new_y), (x, y), std::i32::MAX, check_opponent.1, harvest, check_opponent.0));
                }
                return Some(((new_x, new_y), (x, y), std::i32::MAX, "".to_string(), harvest, attack));
            }
        }
    }

    None
}

// Existing function to search for "A" positions
// fn search_for_a_positions(store_positions_type: &HashMap<POSITION, String>) -> Vec<POSITION> {
//     let mut a_positions = Vec::new();

//     // Iterate over the HashMap and find positions where the value is "A"
//     for (&(x, y), value) in store_positions_type.iter() {
//         if value == "A" {
//             a_positions.push((x, y));
//         }
//     }

//     a_positions
// }

fn is_position_okay_special(
    store_positions_type: &HashMap<POSITION, String>,
    current_position: POSITION,
    width: i32,
    height: i32,
) -> bool {
    if width > 15 && height > 6 {
        if !store_positions_type.contains_key(&(current_position.0, current_position.1)) || (store_positions_type.contains_key(&(current_position.0, current_position.1)) &&
            (store_positions_type[&(current_position.0, current_position.1)] == "A"
            ||store_positions_type[&(current_position.0, current_position.1)] == "B"
            || store_positions_type[&(current_position.0, current_position.1)] == "C"
            || store_positions_type[&(current_position.0, current_position.1)] == "D")) {
            return true;
        } else {
            return false;
        }
    } else if !store_positions_type.contains_key(&(current_position.0, current_position.1)) {
        return true;
    }
    
    false
}

// Existing function to find the next available position to move
fn find_next_position_to_a(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    current_x: i32,
    current_y: i32,
    width: i32,
    height: i32,
) -> Option<POSITION> {
    let directions = [(0, 1), (0, -1), (1, 0), (-1, 0)];

    // If no "A" positions found, fall back to searching for any available position
    for &(dx, dy) in &directions {
        let new_x = current_x + dx;
        let new_y = current_y + dy;
        let type_val = if let Some(v) = store_positions_type.get(&(new_x, new_y)) {
            (v == "A" || v == "B" || v == "C" || v == "D") && (width < 15 || height < 6)
        } else {
            false
        };
        if new_x >= 0 && new_x < width && new_y >= 0 && new_y < height 
            && is_position_okay_special(&store_positions_type, (new_x, new_y), width, height)
            && !my_organ_position.contains(&(new_x, new_y)) && type_val != true {
            return Some((new_x, new_y));
        }
    }

    None
}

fn update_energy_temporary(
    type_organ: &str,
    my_energy: ENERGY
) -> ENERGY {

    let mut new_energy = my_energy;
    if type_organ == "BASIC" {
        new_energy.0 -= 1;
    } else if type_organ == "HARVESTER" {
        new_energy.2 -= 1;
        new_energy.3 -= 1;
    } else if type_organ == "TENTACLE" {
        new_energy.1 -= 1;
        new_energy.2 -= 1;
    } else if type_organ == "SPORER" {
        new_energy.1 -= 1;
        new_energy.3 -= 1;
    } else if type_organ == "ROOT" {
        new_energy.0 -= 1;
        new_energy.1 -= 1;
        new_energy.2 -= 1;
        new_energy.3 -= 1;
    }

    new_energy
}


// Main game loop
fn main() {
    let mut input_line = String::new();
    io::stdin().read_line(&mut input_line).unwrap();
    let inputs = input_line.split(" ").collect::<Vec<_>>();
    let width = parse_input!(inputs[0], i32); // columns in the game grid
    let height = parse_input!(inputs[1], i32); // rows in the game grid
    
    let mut store_owner_organ: HashMap<POSITION, i32> = HashMap::new();
    let mut actual_positions: HashMap<i32, POSITION> = HashMap::new(); 
    let mut proposed_protein_position: Vec<POSITION> = Vec::new();
    let mut sporer_position_id: HashMap<POSITION, i32> = HashMap::new();
    let mut sporer_should_be_launched: HashMap<i32, bool>= HashMap::new();
    let mut sporer_launched: HashMap<i32, bool> = HashMap::new();
    let mut turn = -1;

    let mut harvesting: Vec<POSITION> = Vec::new();
    let mut new_root: (i32, i32) = ( -1, -1);
    let mut root_created: bool = false;

    loop {

        let mut input_line = String::new();
        let mut store_positions_type: HashMap<POSITION, String> = HashMap::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let entity_count = parse_input!(input_line, i32);
        let mut store_id_with_position: HashMap<POSITION, i32> = HashMap::new();
        let mut my_energy: ENERGY;
        let mut opp_energy: ENERGY;
        let mut new_position_each_tour: Vec<POSITION> = Vec::new();
        let mut my_organ_position: Vec<POSITION> = Vec::new();
        let mut opp_organ_position: Vec<POSITION> = Vec::new();

        for _ in 0..entity_count as usize {
            let mut input_line = String::new();
            io::stdin().read_line(&mut input_line).unwrap();
            let inputs = input_line.split(" ").collect::<Vec<_>>();
            let x = parse_input!(inputs[0], i32);
            let y = parse_input!(inputs[1], i32);
            let _type = inputs[2].trim().to_string();
            let owner = parse_input!(inputs[3], i32);
            
            let organ_id = parse_input!(inputs[4], i32);
            let organ_dir = inputs[5].trim().to_string();
            let organ_parent_id = parse_input!(inputs[6], i32);
            let organ_root_id = parse_input!(inputs[7], i32);
            if owner == 1 {
                store_owner_organ.insert((x, y), organ_root_id );
                my_organ_position.push((x, y));
            }
            if owner == 0 {
                opp_organ_position.push((x, y));
            }
            if root_created && _type == "ROOT" && owner == 1 && !actual_positions.contains_key(&organ_id) {
                actual_positions.insert(organ_id, (x, y));
                root_created = false;
            }
            if (organ_id == organ_parent_id && organ_parent_id != 0) && !actual_positions.contains_key(&organ_id) {
                actual_positions.insert(organ_id, (x, y));
            }
            store_positions_type.insert((x, y), _type);
            store_id_with_position.insert((x, y), organ_id);
        }

        if turn == -1 {
            let root_basic_or_a_positions = search_for_root_basic_or_a_in_owner(&store_positions_type, &store_owner_organ);
            if let Some(&(x, y)) = root_basic_or_a_positions.first() {
                let id = search_for_organ_id(&store_id_with_position, (x, y));
                if !actual_positions.contains_key(&id) {
                    actual_positions.insert(id, (x, y));
                } else {
                    actual_positions.entry(id).and_modify(|val| { val.0 = x; val.1 = y; }).or_insert((x, y));
                }
            }
            proposed_protein_position = proposed_proteins(&store_positions_type, &my_organ_position, &opp_organ_position);
            turn = 0;
        }

        // INPUT for my organism's status
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let inputs = input_line.split(" ").collect::<Vec<_>>();
        let my_a = parse_input!(inputs[0], i32);
        let my_b = parse_input!(inputs[1], i32);
        let my_c = parse_input!(inputs[2], i32);
        let my_d = parse_input!(inputs[3], i32); // your protein stock
        my_energy = (my_a, my_b, my_c, my_d);
        
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let inputs = input_line.split(" ").collect::<Vec<_>>();
        let opp_a = parse_input!(inputs[0], i32);
        let opp_b = parse_input!(inputs[1], i32);
        let opp_c = parse_input!(inputs[2], i32);
        let opp_d = parse_input!(inputs[3], i32); // opponent's protein stock
        opp_energy = (opp_a, opp_b, opp_c, opp_d);
        
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let required_actions_count = parse_input!(input_line, i32);
        if turn == 0  || turn == 1 {
            for (&id, &(target_x, target_y)) in actual_positions.clone().iter() {
                if is_sporer_needed(&store_positions_type, &my_organ_position, &proposed_protein_position, my_energy, width, height) {
                    sporer_should_be_launched.insert(id, true);
                    break ;
                }
            }
            turn += 1;
        }

        // for _ in 0..required_actions_count as usize {
        for (&id, &(target_x, target_y)) in actual_positions.clone().iter() {
            // eprintln!("the positions available: {:?}", actual_positions);
            // Check if the sporer should be launched
            if sporer_should_be_launched.contains_key(&id) && sporer_should_be_launched[&id] {
                if let Some(((sporer_x, sporer_y), (organ_x, organ_y), (my_position_x, my_position_y),  direction)) = sporer_position(&store_positions_type,&my_organ_position, &proposed_protein_position, my_energy, width, height) {
                    // eprintln!("1 inside the condition positions are: new: {:?} positions: {:?}", (sporer_x, sporer_y), (organ_x, organ_y));
                    let id_2 = search_for_organ_id(&store_id_with_position, (my_position_x, my_position_y));
                    if !my_organ_position.contains(&(my_position_x, my_position_y)) || new_position_each_tour.contains(&(my_position_x, my_position_y)) || id_2 == -1{
                        println!("WAIT");
                    }
                    else {
                        new_root = (sporer_x, sporer_y);
                        sporer_position_id.insert((organ_x, organ_y), id_2);
                        actual_positions.remove(&id);
                        actual_positions.insert(id_2, (organ_x, organ_y));
                        new_position_each_tour.push((my_position_x, my_position_y));
    
                        sporer_should_be_launched.remove(&id);
                        sporer_launched.insert(id, true);
                        my_energy = update_energy_temporary("SPORER", my_energy);
                        println!("GROW {} {} {} SPORER {}", id_2, organ_x, organ_y, direction);
                    }
                } else {
                    println!("WAIT");
                }
            }
            else if  sporer_launched.contains_key(&id) && sporer_launched[&id] && sporer_position_id.contains_key(&(target_x, target_y)) {
                let id_2 = search_for_organ_id(&store_id_with_position, (target_x, target_y));
                if !my_organ_position.contains(&(target_x, target_y)) || id_2 == -1 {
                    println!("WAIT");
                } else {
                    my_energy = update_energy_temporary("ROOT", my_energy);
                    println!("SPORE {} {} {}", id_2, new_root.0, new_root.1);
                    sporer_position_id.clear();
                    root_created = true;
                    sporer_launched.remove(&id);
                }
            }
            else if let Some(((new_x, new_y), (my_position_x, my_position_y), distance, direction, harvest, attack)) = find_next_position_towards_a(
                &my_organ_position,
                &opp_organ_position,
                &store_owner_organ,
                &store_positions_type,
                &proposed_protein_position,
                my_energy,
                harvesting.clone(),
                (target_x, target_y),
                15,
                width,
                height,
            ) {
                // eprintln!("2 inside the condition positions are: new: {:?} positions: {:?}", (new_x, new_y), (my_position_x, my_position_y));
                let type_organ: OrganType = OrganType::BASIC;
                let id_2 = search_for_organ_id(&store_id_with_position, (my_position_x, my_position_y));
                if !my_organ_position.contains(&(my_position_x, my_position_y)) || new_position_each_tour.contains(&(new_x, new_y)) || id_2 == -1 {
                    println!("WAIT");
                } else {
                    if attack {
                        // It will throw an TENTACLE command to attack nearbe
                        new_position_each_tour.push((new_x, new_y));
                        actual_positions.remove(&id);
                        actual_positions.insert(id_2, (new_x, new_y));
                        my_energy = update_energy_temporary("TENTACLE", my_energy);
                        println!("GROW {} {} {} TENTACLE {}", id_2, new_x, new_y, direction);
                    }
                    else if distance == 1 {
                        // Print special grow command when near "A"
                        new_position_each_tour.push((new_x, new_y));
                        actual_positions.remove(&id);
                        actual_positions.insert(id_2, (new_x, new_y));
                        harvesting.push(harvest);
                        my_energy = update_energy_temporary("HARVESTER", my_energy);
                        println!("GROW {} {} {} HARVESTER {}", id_2, new_x, new_y, direction);
                    } else if type_organ.verify("BASIC", my_energy) {
                        new_position_each_tour.push((new_x, new_y));
                        actual_positions.remove(&id);
                        actual_positions.insert(id_2, (new_x, new_y));
                        my_energy = update_energy_temporary("GROW", my_energy);
                        println!("GROW {} {} {} BASIC", id_2, new_x, new_y);
                    } else {
                        println!("WAIT");
                    }
                }
            } else {
                println!("WAIT");
            }
        }
    }
}
